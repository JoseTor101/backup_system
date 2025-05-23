#include "decompress.h"
#include "crypto.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <zip.h>

using namespace std;

static SimpleCrypto crypto;

// Declaraciones anticipadas
bool reconstructFragmentedFile(const string &outputPath,
                               vector<pair<string, zip_t *>> &archives,
                               const string &fragmentBaseName,
                               int totalFragments, const string &originalPath);

// Parsea un archivo .info y extrae la información
PartInfo parseInfoFile(const string &infoContent) {
  PartInfo info;
  istringstream stream(infoContent);
  string line;

  // Inicializar el hash de encriptación como vacío
  info.encryptionHash = "";

  // Leer la primera línea para obtener el número total de partes
  if (getline(stream, line)) {
    try {
      info.totalParts = stoi(line);
    } catch (...) {
      cerr << "Error al parsear el número total de partes: " << line << endl;
      info.totalParts = 0;
    }
  }

  // Leer la segunda línea para obtener el número de esta parte
  if (getline(stream, line)) {
    try {
      info.partNumber = stoi(line);
    } catch (...) {
      cerr << "Error al parsear el número de parte: " << line << endl;
      info.partNumber = 0;
    }
  }

  // Leer línea siguiente y verificar si es información de encriptación
  while (getline(stream, line)) {
    // Recortar espacios en blanco
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    if (line.find_last_not_of(" \t\r\n") != string::npos)
      line.erase(line.find_last_not_of(" \t\r\n") + 1);

    if (line.empty())
      continue;

    // Verificar si es una línea de encriptación
    if (line.find("encrypted:") == 0) {
      info.encryptionHash = line.substr(10); // Eliminar "encrypted: "
      // Recortar espacios en blanco en el hash
      info.encryptionHash.erase(
          0, info.encryptionHash.find_first_not_of(" \t\r\n"));
      if (info.encryptionHash.find_last_not_of(" \t\r\n") != string::npos)
        info.encryptionHash.erase(
            info.encryptionHash.find_last_not_of(" \t\r\n") + 1);

      cout << "Archivo encriptado detectado (hash: '" << info.encryptionHash
           << "')" << endl;
    } else {
      // Procesar como mapeo de archivos
      size_t pos = line.find(" | ");
      if (pos != string::npos) {
        string zipPath = line.substr(0, pos);
        string remaining = line.substr(pos + 3);

        // Verificar si es un fragmento
        if (zipPath.find(".fragment") != string::npos) {
          regex fragmentRegex("(.+)\\.fragment(\\d+)_of_(\\d+)");
          smatch match;

          if (regex_match(zipPath, match, fragmentRegex) && match.size() == 4) {
            string baseName = match[1];
            int fragNum = stoi(match[2]);
            int totalFrags = stoi(match[3]);

            info.fragments.push_back(
                make_tuple(zipPath, remaining, fragNum, totalFrags));
            info.filePathMapping[zipPath] = remaining;
          } else {
            info.filePathMapping[zipPath] = remaining;
          }
        } else {
          info.filePathMapping[zipPath] = remaining;
        }
      }
    }
  }

  // Leer el mapeo de rutas (incluyendo fragmentos)
  while (getline(stream, line)) {
    if (line.empty())
      continue;

    size_t pos = line.find(" | ");
    if (pos != string::npos) {
      string zipPath = line.substr(0, pos);
      string remaining = line.substr(pos + 3);

      // Verificar si es un fragmento
      if (zipPath.find(".fragment") != string::npos) {
        regex fragmentRegex("(.+)\\.fragment(\\d+)_of_(\\d+)");
        smatch match;

        if (regex_match(zipPath, match, fragmentRegex) && match.size() == 4) {
          string baseName = match[1];
          int fragNum = stoi(match[2]);
          int totalFrags = stoi(match[3]);

          info.fragments.push_back(
              make_tuple(zipPath, remaining, fragNum, totalFrags));
          // También lo agregamos como archivo normal para que aparezca en
          // filePathMapping
          info.filePathMapping[zipPath] = remaining;
        } else {
          // Fragmento con formato inválido, tratarlo como archivo normal
          info.filePathMapping[zipPath] = remaining;
        }
      } else {
        // Archivo normal
        info.filePathMapping[zipPath] = remaining;
      }
    }
  }

  return info;
}

// Función modificada para extraer un archivo encriptado de un ZIP
bool extractFileFromZipWithDecryption(zip_t *archive, const string &zipPath,
                                      const string &outputPath,
                                      const string &password) {
  // Encontrar el archivo en el ZIP
  zip_int64_t index = zip_name_locate(archive, zipPath.c_str(), 0);
  if (index < 0) {
    cerr << "No se encuentra el archivo " << zipPath << " en el ZIP" << endl;
    return false;
  }

  // Abrir el archivo dentro del ZIP
  zip_file_t *zf = zip_fopen_index(archive, index, 0);
  if (!zf) {
    cerr << "No se puede abrir el archivo " << zipPath << " dentro del ZIP"
         << endl;
    return false;
  }

  // Obtener información del archivo
  zip_stat_t stat;
  if (zip_stat_index(archive, index, 0, &stat) < 0) {
    cerr << "No se puede obtener información del archivo " << zipPath << endl;
    zip_fclose(zf);
    return false;
  }

  // Leer todo el contenido en memoria
  vector<unsigned char> buffer(stat.size);
  zip_int64_t bytesRead = zip_fread(zf, buffer.data(), stat.size);
  zip_fclose(zf);

  if (bytesRead < 0 || bytesRead != static_cast<zip_int64_t>(stat.size)) {
    cerr << "Error al leer el archivo completo " << zipPath << endl;
    return false;
  }

  // Decrypt if password is provided
  if (!password.empty()) {
    auto decrypted = crypto.decrypt(buffer.data(), buffer.size(), password);
    buffer = decrypted;
  }

  // Crear directorio destino si no existe
  filesystem::path outputFile(outputPath);
  filesystem::create_directories(outputFile.parent_path());

  // Escribir archivo destino
  ofstream outFile(outputPath, ios::binary);
  if (!outFile) {
    cerr << "No se puede crear el archivo destino " << outputPath << endl;
    return false;
  }

  outFile.write(reinterpret_cast<const char *>(buffer.data()), buffer.size());
  outFile.close();

  cout << "    Extraído" << (password.empty() ? "" : " (desencriptado)") << ": "
       << outputPath << " (" << buffer.size() << " bytes)" << endl;
  return true;
}

// Extrae un archivo específico de un ZIP a la ruta destino
bool extractFileFromZip(zip_t *archive, const string &zipPath,
                        const string &outputPath) {
  return extractFileFromZipWithDecryption(archive, zipPath, outputPath, "");
}

// Modified function to read text file with decryption support
string readTextFileFromZipWithDecryption(zip_t *archive, const string &zipPath,
                                         const string &password) {
  // Primero intentar leer sin desencriptar para ver si es un archivo .info
  if (zipPath.find(".info") != string::npos) {
    // Para archivos .info, intentar primero sin desencriptar
    zip_int64_t index = zip_name_locate(archive, zipPath.c_str(), 0);
    if (index >= 0) {
      zip_file_t *zf = zip_fopen_index(archive, index, 0);
      if (zf) {
        zip_stat_t stat;
        if (zip_stat_index(archive, index, 0, &stat) >= 0) {
          vector<unsigned char> buffer(stat.size);
          zip_int64_t bytesRead = zip_fread(zf, buffer.data(), stat.size);
          zip_fclose(zf);
          if (bytesRead > 0) {
            string content(buffer.begin(), buffer.begin() + bytesRead);
            // Verificar si parece ser un archivo .info válido (debe tener
            // números en las primeras líneas)
            istringstream testStream(content);
            string line1, line2;
            if (getline(testStream, line1) && getline(testStream, line2)) {
              try {
                // Si se puede convertir a entero, es probablemente un archivo
                // .info sin encriptar
                stoi(line1);
                stoi(line2);
                return content; // Devolver sin desencriptar
              } catch (...) {
                // Si falla, sigue con la desencriptación
              }
            }
          }
        }
      }
    }
  }

  // Si no es .info o no pudo ser leído como texto plano, continuar con el
  // proceso normal
  zip_int64_t index = zip_name_locate(archive, zipPath.c_str(), 0);
  if (index < 0) {
    cerr << "No se encuentra el archivo " << zipPath << " en el ZIP" << endl;
    return "";
  }

  zip_file_t *zf = zip_fopen_index(archive, index, 0);
  if (!zf) {
    cerr << "No se puede abrir el archivo " << zipPath << " dentro del ZIP"
         << endl;
    return "";
  }

  zip_stat_t stat;
  if (zip_stat_index(archive, index, 0, &stat) < 0) {
    cerr << "No se puede obtener información del archivo " << zipPath << endl;
    zip_fclose(zf);
    return "";
  }

  vector<unsigned char> buffer(stat.size);
  zip_int64_t bytesRead = zip_fread(zf, buffer.data(), stat.size);
  zip_fclose(zf);

  if (bytesRead < 0) {
    cerr << "Error al leer el archivo " << zipPath << endl;
    return "";
  }

  // Decrypt if password is provided
  if (!password.empty()) {
    auto decrypted = crypto.decrypt(buffer.data(), bytesRead, password);
    return string(decrypted.begin(), decrypted.end());
  }

  return string(buffer.begin(), buffer.begin() + bytesRead);
}

// Leer el contenido de un archivo de texto desde un ZIP
string readTextFileFromZip(zip_t *archive, const string &zipPath) {
  return readTextFileFromZipWithDecryption(archive, zipPath, "");
}

// Función modificada para descomprimir partes con o sin contraseña
bool decompressPartsWithPassword(const string &folderPath,
                                 const string &outputPath,
                                 const string &password) {
  // Buscar todos los archivos ZIP en el directorio especificado
  vector<filesystem::path> zipFiles;
  for (const auto &entry : filesystem::directory_iterator(folderPath)) {
    if (entry.is_regular_file() && entry.path().extension() == ".zip") {
      zipFiles.push_back(entry.path());
    }
  }

  if (zipFiles.empty()) {
    cerr << "No se encontraron archivos ZIP en " << folderPath << endl;
    return false;
  }

  cout << "Se encontraron " << zipFiles.size()
       << " archivos ZIP para descomprimir" << endl;
  if (!password.empty()) {
    cout << "Modo desencriptado activado" << endl;
    cout << "Hash de verificación: " << crypto.generatePasswordHash(password)
         << endl;
  }

  // Mantener una lista de todos los archivos ZIP abiertos para buscar
  // fragmentos
  vector<pair<string, zip_t *>> allArchives;
  map<string, vector<tuple<string, string, int, int>>> allFragments;

  // Primera pasada: recopilar información de todos los fragmentos
  bool encryptionDetected = false;
  string detectedHash = "";

  for (const auto &zipFile : zipFiles) {
    int err = 0;
    zip_t *archive = zip_open(zipFile.string().c_str(), 0, &err);
    if (!archive) {
      char errStr[128];
      zip_error_to_str(errStr, sizeof(errStr), err, errno);
      cerr << "Error al abrir ZIP " << zipFile << ": " << errStr << endl;
      continue;
    }

    // Añadir a la lista de archivos
    allArchives.push_back({zipFile.string(), archive});

    // Buscar el archivo .info dentro del ZIP
    string infoFileName;
    zip_int64_t numEntries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < numEntries; i++) {
      const char *name = zip_get_name(archive, i, 0);
      if (name && strstr(name, ".info") != nullptr) {
        infoFileName = name;
        break;
      }
    }

    if (infoFileName.empty()) {
      cerr << "No se encontró archivo .info en " << zipFile << endl;
      continue;
    }

    // Leer y parsear el archivo .info (try both encrypted and unencrypted)
    string infoContent =
        readTextFileFromZipWithDecryption(archive, infoFileName, password);
    if (infoContent.empty() && !password.empty()) {
      // Try without decryption in case .info is not encrypted
      infoContent = readTextFileFromZip(archive, infoFileName);
    }

    if (infoContent.empty()) {
      cerr << "No se pudo leer el archivo .info en " << zipFile << endl;
      continue;
    }

    PartInfo info = parseInfoFile(infoContent);

    // Check for encryption and verify password if provided
    if (!info.encryptionHash.empty()) {
      encryptionDetected = true;
      detectedHash = info.encryptionHash;
      cout << "Detectado archivo encriptado con hash: " << info.encryptionHash
           << endl;

      if (!password.empty()) {
        string providedHash = crypto.generatePasswordHash(password);
        cout << "Contraseña proporcionada con hash: " << providedHash << endl;

        if (providedHash != info.encryptionHash) {
          cerr << "\n";
          cerr << "╔══════════════════════════════════════════════════════════╗"
               << endl;
          cerr << "║                ¡ERROR DE AUTENTICACIÓN!                 ║"
               << endl;
          cerr << "╠══════════════════════════════════════════════════════════╣"
               << endl;
          cerr << "║ La contraseña proporcionada es incorrecta.              ║"
               << endl;
          cerr << "║ No se puede desencriptar el archivo.                    ║"
               << endl;
          cerr << "╚══════════════════════════════════════════════════════════╝"
               << endl;
          cerr << "\n";
          cerr << "Hash esperado:    " << info.encryptionHash << endl;
          cerr << "Hash recibido:    " << providedHash << endl;
          cerr << "Intente de nuevo con la contraseña correcta usando: -p "
                  "[contraseña]"
               << endl;

          // Cerrar archivos abiertos
          for (auto &[path, arch] : allArchives) {
            zip_close(arch);
          }
          return false;
        } else {
          cout << "✓ Contraseña correcta verificada!" << endl;
        }
      } else {
        cerr << "\n";
        cerr << "╔══════════════════════════════════════════════════════════╗"
             << endl;
        cerr << "║                ¡ARCHIVO ENCRIPTADO!                     ║"
             << endl;
        cerr << "╠══════════════════════════════════════════════════════════╣"
             << endl;
        cerr << "║ Los archivos están protegidos con contraseña.           ║"
             << endl;
        cerr << "║ Debe proporcionar la contraseña para desencriptar.      ║"
             << endl;
        cerr << "╚══════════════════════════════════════════════════════════╝"
             << endl;
        cerr << "\n";
        cerr << "Use el parámetro -p [contraseña] para proporcionar la "
                "contraseña."
             << endl;

        // Cerrar archivos abiertos
        for (auto &[path, arch] : allArchives) {
          zip_close(arch);
        }
        return false;
      }
    }

    cout << "  Parte " << info.partNumber << " de " << info.totalParts
         << " con " << info.filePathMapping.size() << " archivos"
         << (info.encryptionHash.empty() ? "" : " (encriptada)") << endl;

    // Registrar todos los fragmentos encontrados
    for (const auto &[zipPath, originalPath, fragNum, totalFrags] :
         info.fragments) {
      string baseName = zipPath.substr(0, zipPath.find(".fragment"));

      if (allFragments.find(baseName) == allFragments.end()) {
        allFragments[baseName] = vector<tuple<string, string, int, int>>();
      }

      allFragments[baseName].push_back(
          make_tuple(zipPath, originalPath, fragNum, totalFrags));
    }
  }

  // Segunda pasada: procesar archivos normales
  for (const auto &[zipPath, archive] : allArchives) {
    cout << "Procesando " << zipPath << "..." << endl;

    // Buscar el archivo .info dentro del ZIP
    string infoFileName;
    zip_int64_t numEntries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < numEntries; i++) {
      const char *name = zip_get_name(archive, i, 0);
      if (name && strstr(name, ".info") != nullptr) {
        infoFileName = name;
        break;
      }
    }

    if (infoFileName.empty()) {
      cerr << "No se encontró archivo .info en " << zipPath << endl;
      continue;
    }

    // Leer y parsear el archivo .info
    string infoContent =
        readTextFileFromZipWithDecryption(archive, infoFileName, password);
    if (infoContent.empty() && !password.empty()) {
      infoContent = readTextFileFromZip(archive, infoFileName);
    }

    if (infoContent.empty()) {
      cerr << "No se pudo leer el archivo .info en " << zipPath << endl;
      continue;
    }

    PartInfo info = parseInfoFile(infoContent);

    // Extraer archivos normales (no fragmentos)
    for (const auto &[zipPath, originalPath] : info.filePathMapping) {
      // Saltar fragmentos y archivos de información
      if (zipPath.find(".fragment") != string::npos ||
          zipPath.find(".info") != string::npos) {
        continue;
      }

      // Construir la ruta de salida manteniendo la estructura de carpetas
      filesystem::path destPath = filesystem::path(outputPath) / zipPath;

      cout << "  Extrayendo " << zipPath << " a " << destPath << endl;

      if (!extractFileFromZipWithDecryption(archive, zipPath, destPath.string(),
                                            password)) {
        cerr << "  Error al extraer " << zipPath << endl;
      }
    }
  }

  // Tercera pasada: reconstruir archivos fragmentados
  for (const auto &[baseName, fragments] : allFragments) {
    // Obtener el número total de fragmentos del primer fragmento
    if (fragments.empty())
      continue;

    const auto &[zipPath, originalPath, fragNum, totalFrags] = fragments[0];

    // Verificar que tenemos todos los fragmentos
    set<int> foundFragNumbers;
    for (const auto &[_, __, fragNum, ___] : fragments) {
      foundFragNumbers.insert(fragNum);
    }

    if (foundFragNumbers.size() != static_cast<size_t>(totalFrags)) {
      cerr << "¡Advertencia! No se encontraron todos los fragmentos para "
           << baseName << ". Encontrados: " << foundFragNumbers.size() << " de "
           << totalFrags << endl;
      continue;
    }

    cout << "Reconstruyendo archivo fragmentado: " << baseName << endl;

    // Ruta de salida para el archivo reconstruido (mantener estructura de
    // carpetas)
    filesystem::path outputFilePath = filesystem::path(outputPath) / baseName;

    // Asegurarse que el directorio existe
    filesystem::create_directories(outputFilePath.parent_path());

    ofstream outFile(outputFilePath, ios::binary);

    if (!outFile) {
      cerr << "No se pudo crear el archivo reconstruido: " << outputFilePath
           << endl;
      continue;
    }

    bool reconstructionSuccess = true;

    // Ordenar fragmentos por número
    vector<tuple<string, string, int, int>> sortedFragments = fragments;
    sort(sortedFragments.begin(), sortedFragments.end(),
         [](const auto &a, const auto &b) { return get<2>(a) < get<2>(b); });

    // Procesar cada fragmento en orden
    for (const auto &[fragZipPath, fragOrigPath, fragNumber, _] :
         sortedFragments) {
      bool fragFound = false;

      // Buscar en qué archivo ZIP está este fragmento
      for (const auto &[archivePath, archive] : allArchives) {
        zip_int64_t index = zip_name_locate(archive, fragZipPath.c_str(), 0);
        if (index >= 0) {
          // Encontramos el fragmento, extraerlo
          zip_file_t *zf = zip_fopen_index(archive, index, 0);
          if (!zf) {
            cerr << "Error al abrir fragmento: " << fragZipPath << endl;
            reconstructionSuccess = false;
            break;
          }

          zip_stat_t stat;
          if (zip_stat_index(archive, index, 0, &stat) < 0) {
            cerr << "Error al obtener información de fragmento: " << fragZipPath
                 << endl;
            zip_fclose(zf);
            reconstructionSuccess = false;
            break;
          }

          // Read entire fragment into memory
          vector<unsigned char> buffer(stat.size);
          zip_int64_t bytesRead = zip_fread(zf, buffer.data(), stat.size);
          zip_fclose(zf);

          if (bytesRead < 0 ||
              bytesRead != static_cast<zip_int64_t>(stat.size)) {
            cerr << "Error al leer fragmento completo: " << fragZipPath << endl;
            reconstructionSuccess = false;
            break;
          }

          // Decrypt fragment if password is provided
          if (!password.empty()) {
            auto decrypted =
                crypto.decrypt(buffer.data(), buffer.size(), password);
            buffer = decrypted;
          }

          // Write fragment to output file
          outFile.write(reinterpret_cast<const char *>(buffer.data()),
                        buffer.size());
          if (!outFile) {
            cerr << "Error al escribir fragmento al archivo de salida" << endl;
            reconstructionSuccess = false;
            break;
          }

          fragFound = true;
          cout << "  Procesado fragmento"
               << (password.empty() ? "" : " (desencriptado)") << " "
               << fragNumber << " de " << totalFrags << " ("
               << (buffer.size() / 1024) << "KB)" << endl;
          break;
        }
      }

      if (!fragFound) {
        cerr << "No se encontró el fragmento: " << fragZipPath << endl;
        reconstructionSuccess = false;
        break;
      }
    }

    outFile.close();

    if (reconstructionSuccess) {
      cout << "Archivo reconstruido correctamente: " << outputFilePath << " ("
           << (filesystem::file_size(outputFilePath) / 1024 / 1024) << "MB)"
           << endl;
    } else {
      cerr << "Error al reconstruir archivo fragmentado: " << baseName << endl;
    }
  }

  // Cerrar todos los archivos ZIP al finalizar
  for (auto &[path, archive] : allArchives) {
    zip_close(archive);
  }

  cout << "Descompresión" << (password.empty() ? "" : " y desencriptado")
       << " completada en " << outputPath << endl;
  return true;
}

// Función principal para descomprimir partes
bool decompressParts(const string &folderPath, const string &outputPath) {
  return decompressPartsWithPassword(folderPath, outputPath, "");
}

int main(int argc, char *argv[]) {
  string inputFolder = "./output";
  string outputFolder = "./extracted";
  string password = "";

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    if (string(argv[i]) == "-i" && i + 1 < argc) {
      inputFolder = argv[i + 1];
      i++;
    } else if (string(argv[i]) == "-o" && i + 1 < argc) {
      outputFolder = argv[i + 1];
      i++;
    } else if (string(argv[i]) == "-p" && i + 1 < argc) {
      password = argv[i + 1];
      i++;
    } else if (string(argv[i]) == "-h" || string(argv[i]) == "--help") {
      cout << "Uso: decompressor [-i carpeta_entrada] [-o carpeta_salida] [-p "
              "contraseña]"
           << endl;
      cout << "  -i : Directorio con archivos ZIP (default: ./output)" << endl;
      cout << "  -o : Directorio de salida (default: ./extracted)" << endl;
      cout << "  -p : Contraseña para desencriptar (opcional)" << endl;
      cout << "  -h : Mostrar esta ayuda" << endl;
      return 0;
    } else if (i == 1) {
      inputFolder = argv[i];
    } else if (i == 2) {
      outputFolder = argv[i];
    }
  }

  // Asegurar que el directorio de salida exista
  filesystem::create_directories(outputFolder);

  cout << "Descomprimiendo archivos de " << inputFolder << " a " << outputFolder
       << endl;

  if (password != "") {
    if (decompressPartsWithPassword(inputFolder, outputFolder, password)) {
      cout << "Operación completada con éxito." << endl;
      return 0;
    } else {
      cerr << "Error durante la operación." << endl;
      return 1;
    }
  } else {
    if (decompressParts(inputFolder, outputFolder)) {
      cout << "Operación completada con éxito." << endl;
      return 0;
    } else {
      cerr << "Error durante la operación." << endl;
      return 1;
    }
  }
}