#include "crypto.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <omp.h>
#include <set>
#include <stdio.h>
#include <string>
#include <vector>
#include <zip.h>

using namespace std;

static SimpleCrypto crypto;

// Función para verificar si un archivo debe ser ignorado según los patrones
bool shouldIgnoreFile(const string &relativePath,
                      const set<string> &ignorePatterns) {
  // Si el archivo está vacío, no ignorarlo (caso edge)
  if (relativePath.empty()) {
    return false;
  }

  // Comprobar cada patrón de ignore
  for (const auto &pattern : ignorePatterns) {
    // Si el patrón comienza con /, es una ruta relativa desde la raíz
    if (!pattern.empty() && pattern[0] == '/') {
      string cleanPattern = pattern.substr(1); // Quitar el / inicial
      // Verificar si la ruta comienza con el patrón (para ignorar directorios
      // completos)
      if (relativePath == cleanPattern ||
          relativePath.find(cleanPattern + "/") == 0) {
        return true;
      }
    }
    // Patrón con comodín al final (ej: "*.txt")
    else if (!pattern.empty() && pattern.back() == '*') {
      string prefix = pattern.substr(0, pattern.length() - 1);
      if (relativePath.length() >= prefix.length() &&
          relativePath.substr(0, prefix.length()) == prefix) {
        return true;
      }
    }
    // Coincidencia exacta
    else if (relativePath == pattern) {
      return true;
    }
    // Verificar si es un archivo dentro del directorio especificado
    else if (relativePath.find(pattern + "/") == 0) {
      return true;
    }
  }

  return false;
}

// Función para recolectar todos los archivos no ignorados en un directorio
vector<filesystem::path> collectFiles(const string &folderPath,
                                      const set<string> &ignorePatterns) {
  vector<filesystem::path> allFiles;

  // Recorrer el filesystem (aún secuencial) y guardar todos los archivos
  // regulares
  vector<filesystem::directory_entry> entries;
  for (const auto &entry :
       filesystem::recursive_directory_iterator(folderPath)) {
    if (entry.is_regular_file()) {
      entries.push_back(entry);
    }
  }

  // Filtrar en paralelo los archivos a ignorar
  vector<filesystem::path> tempFiles;
  std::mutex mtx;

#pragma omp parallel
  {
    vector<filesystem::path> localFiles;

#pragma omp for nowait // No necesita esperar que todos los hilos terminen el
                       // bucle
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
      const auto &entry = entries[i];
      string relativePath =
          filesystem::relative(entry.path(), folderPath).string();

      // Comprobar si el archivo debe ser ignorado
      bool shouldIgnore = false;

      // Si es el propio archivo .ignore, siempre ignorarlo
      if (entry.path().filename() == ".ignore") {
        shouldIgnore = true;
      } else {
        // Usar la función de verificación de patrones
        shouldIgnore = shouldIgnoreFile(relativePath, ignorePatterns);
      }

      if (!shouldIgnore) {
        localFiles.push_back(entry.path());
      }
    }

// Fusionar resultados en una sola lista con exclusión mutua
#pragma omp critical // Protege esta sección para que solo un hilo a la vez
                     // pueda ejecutarla
    allFiles.insert(allFiles.end(), localFiles.begin(), localFiles.end());
  }

  return allFiles;
}

// función para leer patrones a ignorar desde un archivo .ignore
set<string> readIgnorePatterns(const string &folderPath) {
  set<string> patterns;

  // Ruta del archivo .ignore
  filesystem::path ignorePath = filesystem::path(folderPath) / ".ignore";

  // Verificar si existe el archivo .ignore
  if (filesystem::exists(ignorePath)) {
    cout << "Leyendo patrones de ignorar desde " << ignorePath << endl;

    // Leer el archivo línea por línea
    ifstream ignoreFile(ignorePath);
    string line;
    while (getline(ignoreFile, line)) {
      // Eliminar espacios en blanco al principio y al final
      line.erase(0, line.find_first_not_of(" \t\r\n"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);

      // Ignorar líneas vacías y comentarios
      if (line.empty() || line[0] == '#') {
        continue;
      }

      patterns.insert(line);
    }

    cout << "Se cargaron " << patterns.size() << " patrones para ignorar."
         << endl;
  } else {
    cout << "No se encontró archivo .ignore, no se ignorará ningún archivo."
         << endl;
  }

  return patterns;
}

// Función mejorada para añadir un buffer de memoria a un ZIP con opción de
// copiar
bool addBufferToZip(zip_t *archive, const char *buffer, size_t bufferSize,
                    const string &zipPath, bool makeCopy = false,
                    bool freeBuffer = true, bool *overallSuccess = nullptr,
                    bool *opSuccess = nullptr) {

  // Opcionalmente crear una copia del buffer
  char *finalBuffer = (char *)buffer;
  if (makeCopy) {
    finalBuffer = new char[bufferSize];
    memcpy(finalBuffer, buffer, bufferSize);
  }

  // Crear una fuente ZIP con los datos
  zip_source_t *source =
      zip_source_buffer(archive, finalBuffer, bufferSize, freeBuffer ? 1 : 0);
  if (source == nullptr) {
    cerr << "Error al crear fuente ZIP para " << zipPath << ": "
         << zip_strerror(archive) << endl;
    if (makeCopy && freeBuffer) {
      delete[] finalBuffer;
    }
    if (overallSuccess)
      *overallSuccess = false;
    if (opSuccess)
      *opSuccess = false;
    return false;
  }

  // Agregar el buffer al ZIP
  zip_int64_t index =
      zip_file_add(archive, zipPath.c_str(), source, ZIP_FL_ENC_UTF_8);
  if (index < 0) {
    cerr << "Error al añadir " << zipPath
         << " al ZIP: " << zip_strerror(archive) << endl;
    zip_source_free(source);
    if (overallSuccess)
      *overallSuccess = false;
    if (opSuccess)
      *opSuccess = false;
    return false;
  }

  return true;
}

// Función para añadir un archivo de texto en memoria al ZIP (usado en el .info)
bool addTextToZip(zip_t *archive, const string &content,
                  const string &zipPath) {
  // Crea una copia del contenido en un buffer nuevo
  // con explícita asignación de memoria para evitar problemas

  return addBufferToZip(archive, content.data(), content.size(), zipPath, true,
                        true);
}

bool addFileToZip(zip_t *archive, const string &filePath,
                  const string &zipPath) {
  // Leer todo el archivo en memoria primero
  ifstream file(filePath, ios::binary);
  if (!file) {
    cerr << "No se pudo abrir el archivo: " << filePath << endl;
    return false;
  }

  // Obtener el tamaño del archivo
  file.seekg(0, ios::end);
  streamsize size = file.tellg();
  file.seekg(0, ios::beg);

  // Crear un buffer para el contenido del archivo
  auto fileContent = new char[size];
  if (!file.read(fileContent, size)) {
    cerr << "Error al leer el archivo: " << filePath << endl;
    delete[] fileContent;
    return false;
  }

  return addBufferToZip(archive, fileContent, size, zipPath, false, true);
}

bool addEncryptedBufferToZip(zip_t *archive, const char *buffer,
                             size_t bufferSize, const string &zipPath,
                             const string &password = "", bool makeCopy = false,
                             bool freeBuffer = true,
                             bool *overallSuccess = nullptr,
                             bool *opSuccess = nullptr) {

  char *finalBuffer = nullptr;
  size_t finalSize = bufferSize;

  if (!password.empty()) {
    // Encriptar el buffer
    auto encrypted = crypto.encrypt(
        reinterpret_cast<const unsigned char *>(buffer), bufferSize, password);
    finalSize = encrypted.size();
    finalBuffer = new char[finalSize];
    memcpy(finalBuffer, encrypted.data(), finalSize);
  } else {
    // Sin encriptado, usar buffer original
    if (makeCopy) {
      finalBuffer = new char[bufferSize];
      memcpy(finalBuffer, buffer, bufferSize);
    } else {
      finalBuffer = const_cast<char *>(buffer);
    }
  }

  // Crear fuente ZIP
  zip_source_t *source = zip_source_buffer(
      archive, finalBuffer, finalSize,
      (!password.empty() || (makeCopy && freeBuffer)) ? 1 : 0);
  if (source == nullptr) {
    cerr << "Error al crear fuente ZIP para " << zipPath << ": "
         << zip_strerror(archive) << endl;
    if (!password.empty() || (makeCopy && freeBuffer)) {
      delete[] finalBuffer;
    }
    if (overallSuccess)
      *overallSuccess = false;
    if (opSuccess)
      *opSuccess = false;
    return false;
  }

  // Agregar al ZIP
  zip_int64_t index =
      zip_file_add(archive, zipPath.c_str(), source, ZIP_FL_ENC_UTF_8);
  if (index < 0) {
    cerr << "Error al añadir " << zipPath
         << " al ZIP: " << zip_strerror(archive) << endl;
    zip_source_free(source);
    if (overallSuccess)
      *overallSuccess = false;
    if (opSuccess)
      *opSuccess = false;
    return false;
  }

  return true;
}

// Función modificada para añadir archivo encriptado al ZIP
bool addEncryptedFileToZip(zip_t *archive, const string &filePath,
                           const string &zipPath, const string &password = "") {
  // Leer archivo
  ifstream file(filePath, ios::binary);
  if (!file) {
    cerr << "No se pudo abrir el archivo: " << filePath << endl;
    return false;
  }

  file.seekg(0, ios::end);
  streamsize size = file.tellg();
  file.seekg(0, ios::beg);

  auto fileContent = new char[size];
  if (!file.read(fileContent, size)) {
    cerr << "Error al leer el archivo: " << filePath << endl;
    delete[] fileContent;
    return false;
  }

  bool result = addEncryptedBufferToZip(archive, fileContent, size, zipPath,
                                        password, false, true);
  return result;
}

// Función para calcular el número de partes necesarias según el tamaño de los
// archivos
int calculateTotalParts(const vector<filesystem::path> &allFiles,
                        size_t maxSizeBytes, int maxSizeMB) {
  size_t estimatedSize = 0;
  int estimatedParts = 0;
  bool afterBigFile =
      false; // Para rastrear si estamos después de un archivo grande

  for (const auto &filePath : allFiles) {
    uintmax_t fileSize = filesystem::file_size(filePath);

    if (fileSize > maxSizeBytes) {
      // Archivo grande
      int fragmentsNeeded =
          static_cast<int>((fileSize + maxSizeBytes - 1) / maxSizeBytes);
      estimatedParts += fragmentsNeeded;
      afterBigFile = true; // Marcar que venimos de un archivo grande
      estimatedSize = 0;   // Resetear acumulador
    } else {
      // Archivo normal
      if (afterBigFile || estimatedSize + fileSize > maxSizeBytes) {
        // Si venimos de un archivo grande o excedemos el tamaño,
        // necesitamos una nueva parte
        estimatedParts++;
        estimatedSize = fileSize;
        afterBigFile = false; // Ya no venimos de un archivo grande
      } else {
        estimatedSize += fileSize;
      }
    }
  }

  // No olvidar la última parte si hay archivos pendientes
  if (estimatedSize > 0) {
    estimatedParts++;
  }

  int totalParts = estimatedParts > 0 ? estimatedParts : 1;
  cout << "Dividiendo en aproximadamente " << totalParts << " partes de hasta "
       << maxSizeMB << "MB cada una." << endl;

  return totalParts;
}

// Versión optimizada para procesar archivos grandes con paralelismo eficiente
bool processLargeFile(const filesystem::path &filePath,
                      const string &relativePath, const string &folderPath,
                      size_t maxSizeBytes, const string &baseName,
                      const string &extension,
                      const filesystem::path &outputDir, int &part,
                      int &totalParts, int &totalFragments,
                      bool &overallSuccess, const string &password = "") {

  bool isEncrypted = !password.empty();
  uintmax_t fileSize = filesystem::file_size(filePath);

  cout << "  Archivo grande detectado: " << relativePath << " ("
       << (fileSize / 1024 / 1024) << "MB)" << endl;

  int fragmentsNeeded =
      static_cast<int>((fileSize + maxSizeBytes - 1) / maxSizeBytes);
  int startingPart = part + 1;

  // Actualizar totalParts si es necesario
  if (fragmentsNeeded > totalParts) {
    totalParts = fragmentsNeeded;
  }

  cout << "  Dividiendo en " << fragmentsNeeded << " archivos ZIP"
       << (isEncrypted ? " encriptados" : "") << "..." << endl;

  // Preparamos los argumentos para cada hilo
  struct FragmentTask {
    int fragNum;
    streamsize offset;
    streamsize bytesToRead;
    string fragmentName;
    int localPart;
    string partFileName;
    filesystem::path partPath;
    bool success;
    vector<char> buffer; // Cada tarea tiene su propio buffer
  };

  // Preparar todas las tareas antes de la ejecución paralela
  vector<FragmentTask> tasks(fragmentsNeeded);
  for (int fragNum = 0; fragNum < fragmentsNeeded; fragNum++) {
    tasks[fragNum].fragNum = fragNum;
    tasks[fragNum].offset = fragNum * maxSizeBytes;
    tasks[fragNum].bytesToRead = min(
        maxSizeBytes, static_cast<size_t>(fileSize - tasks[fragNum].offset));
    tasks[fragNum].localPart = startingPart + fragNum;
    tasks[fragNum].fragmentName = relativePath + ".fragment" +
                                  to_string(fragNum + 1) + "_of_" +
                                  to_string(fragmentsNeeded);
    tasks[fragNum].partFileName = baseName + "_part" +
                                  to_string(tasks[fragNum].localPart) + "_of_" +
                                  to_string(totalParts) + extension;
    tasks[fragNum].partPath = outputDir / tasks[fragNum].partFileName;
    tasks[fragNum].success = true;
    tasks[fragNum].buffer.resize(
        tasks[fragNum].bytesToRead); // Pre-asignar el buffer
  }

// MEJORA 1: Usar lecturas directas del sistema operativo para cada fragmento
#pragma omp parallel for schedule(dynamic)
  for (int i = 0; i < fragmentsNeeded; i++) {
    // Abrir el archivo de forma independiente para cada fragmento (evita la
    // contención del mutex)
    FILE *file = fopen(filePath.string().c_str(), "rb");
    if (!file) {
#pragma omp critical
      cerr << "  Error al abrir archivo grande para el fragmento " << i + 1
           << endl;
      tasks[i].success = false;
      continue;
    }

    // Posicionar y leer directamente
    if (fseeko(file, tasks[i].offset, SEEK_SET) != 0 ||
        fread(tasks[i].buffer.data(), 1, tasks[i].bytesToRead, file) !=
            tasks[i].bytesToRead) {
#pragma omp critical
      cerr << "  Error al leer fragmento " << i + 1 << endl;
      tasks[i].success = false;
      fclose(file);
      continue;
    }

    fclose(file);

#pragma omp critical
    cout << "    Fragmento " << i + 1 << " leído correctamente ("
         << tasks[i].bytesToRead / 1024 << "KB)" << endl;
  }

  // MEJORA 2: Optimizar la creación de archivos ZIP y encriptación
  std::atomic<bool> atomicSuccess{true};
  std::atomic<int> completedFragments{0};

  // MEJORA 3: Ajustar dinámicamente la granularidad
  int chunksPerThread =
      std::max(1, fragmentsNeeded / (omp_get_max_threads() * 2));

#pragma omp parallel for schedule(dynamic, chunksPerThread)
  for (int i = 0; i < fragmentsNeeded; i++) {
    // Si la lectura falló, omitir este fragmento
    if (!tasks[i].success)
      continue;

    // Crear archivo ZIP
    int zip_error = 0;
    zip_t *archive = zip_open(tasks[i].partPath.string().c_str(),
                              ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!archive) {
      char errstr[128];
      zip_error_to_str(errstr, sizeof(errstr), zip_error, errno);
#pragma omp critical
      cerr << "No se pudo crear el archivo ZIP: " << tasks[i].partPath << " - "
           << errstr << endl;
      tasks[i].success = false;
      atomicSuccess = false;
      continue;
    }

    // MEJORA 4: Encriptación más eficiente
    bool addSuccess = false;
    if (isEncrypted) {
      // Encriptar solo el buffer ya cargado
      addSuccess = addEncryptedBufferToZip(
          archive, tasks[i].buffer.data(), tasks[i].buffer.size(),
          tasks[i].fragmentName, password, true, false);
    } else {
      addSuccess = addBufferToZip(archive, tasks[i].buffer.data(),
                                  tasks[i].buffer.size(), tasks[i].fragmentName,
                                  true, false);
    }

    // Liberar memoria del buffer una vez que se haya utilizado
    vector<char>().swap(tasks[i].buffer);

    if (!addSuccess) {
      zip_close(archive);
      tasks[i].success = false;
      atomicSuccess = false;
      continue;
    }

    // Crear y añadir archivo .info
    ostringstream fragInfoContent;
    fragInfoContent << totalParts << "\n" << tasks[i].localPart << "\n";
    if (isEncrypted) {
      fragInfoContent << "encrypted: " << crypto.generatePasswordHash(password)
                      << "\n";
    }
    fragInfoContent << tasks[i].fragmentName << " | " << filePath.string()
                    << "\n";

    string infoStr = fragInfoContent.str();
    if (!addBufferToZip(archive, infoStr.data(), infoStr.size(),
                        "part_" + to_string(tasks[i].localPart) + ".info", true,
                        true)) {
#pragma omp critical
      cerr << "  Error al agregar archivo de información al fragmento "
           << tasks[i].fragNum << endl;
      tasks[i].success = false;
      atomicSuccess = false;
    }

    if (zip_close(archive) < 0) {
#pragma omp critical
      cerr << "Error al cerrar el archivo ZIP: " << tasks[i].partPath << endl;
      tasks[i].success = false;
      atomicSuccess = false;
    }

    // Incrementar contador de fragmentos completados y mostrar progreso
    int completed = ++completedFragments;
#pragma omp critical
    {
      cout << "    Fragmento " << tasks[i].fragNum + 1 << " de "
           << fragmentsNeeded << " (" << (tasks[i].bytesToRead / 1024)
           << "KB) completado - Progreso: " << completed << "/"
           << fragmentsNeeded << endl;
    }
  }

  // MEJORA 5: Eliminar dependencias y actualizar contadores más eficientemente
  part += fragmentsNeeded;
  totalFragments += fragmentsNeeded;

  // Verificar resultado final
  overallSuccess = atomicSuccess;
  if (!overallSuccess) {
    cerr << "Error al fragmentar el archivo " << filePath << endl;
  } else {
    cout << "  Archivo fragmentado correctamente: " << relativePath << endl;
  }

  return overallSuccess;
}
bool processNormalFiles(vector<filesystem::path> &allFiles, size_t &fileIndex,
                        const string &folderPath, size_t maxSizeBytes,
                        const string &baseName, const string &extension,
                        const filesystem::path &outputDir, int part,
                        int totalParts, bool &overallSuccess,
                        const string &password = "") {

  bool isEncrypted = !password.empty();
  string partFileName = baseName + "_part" + to_string(part) + "_of_" +
                        to_string(totalParts) + extension;
  filesystem::path partPath = outputDir / partFileName;

  // Abrir el archivo ZIP para esta parte
  int zip_error = 0;
  zip_t *archive = zip_open(partPath.string().c_str(),
                            ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
  if (!archive) {
    char errstr[128];
    zip_error_to_str(errstr, sizeof(errstr), zip_error, errno);
    cerr << "No se pudo crear el archivo ZIP: " << partPath << " - " << errstr
         << endl;
    overallSuccess = false;
    return false;
  }

  bool partSuccess = true;
  size_t currentSize = 0;

  // Crear archivo .info básico para esta parte
  ostringstream infoContent;
  infoContent << totalParts << "\n";
  infoContent << part << "\n";

  // Añadir información de encriptación si hay contraseña
  if (isEncrypted) {
    infoContent << "encrypted: " << crypto.generatePasswordHash(password)
                << "\n";
    cout << "  Usando encriptación para parte " << part << endl;
  }

  // Procesar archivos para esta parte
  while (fileIndex < allFiles.size()) {
    uintmax_t fileSize = filesystem::file_size(allFiles[fileIndex]);
    string relativePath =
        filesystem::relative(allFiles[fileIndex], folderPath).string();

    // Si es un archivo grande, salir del bucle
    if (fileSize > maxSizeBytes) {
      break;
    }

    // Si añadir este archivo excede el tamaño máximo, terminar esta parte
    if (currentSize > 0 && (currentSize + fileSize) > maxSizeBytes) {
      break;
    }

    // Agregar archivo al ZIP
    cout << "  Agregando" << (isEncrypted ? " (encriptado)" : "") << ": "
         << relativePath << " (" << (fileSize / 1024) << "KB)" << endl;

    bool success = false;
    if (isEncrypted) {
      success = addEncryptedFileToZip(archive, allFiles[fileIndex].string(),
                                      relativePath, password);
    } else {
      success =
          addFileToZip(archive, allFiles[fileIndex].string(), relativePath);
    }

    if (!success) {
      cerr << "  Error al agregar: " << allFiles[fileIndex] << endl;
      partSuccess = false;
      overallSuccess = false;
    } else {
      // Añadir información del archivo
      infoContent << relativePath << " | " << allFiles[fileIndex].string()
                  << "\n";
      currentSize += fileSize;
    }

    fileIndex++;
  }

  // Añadir el archivo .info al ZIP (siempre sin encriptar)
  string infoStr = infoContent.str();
  if (!addBufferToZip(archive, infoStr.data(), infoStr.size(),
                      "part_" + to_string(part) + ".info", true, true)) {
    cerr << "  Error al agregar archivo de información" << endl;
    partSuccess = false;
    overallSuccess = false;
  } else {
    cout << "  Agregado: part_" << part << ".info (Información de rutas)"
         << endl;
  }

  // Cerrar el archivo ZIP
  if (zip_close(archive) < 0) {
    cerr << "Error al cerrar el archivo ZIP: " << partPath << endl;
    overallSuccess = false;
    partSuccess = false;
  }

  return partSuccess;
}

// Función principal unificada con soporte explícito para control de paralelismo
bool compressFolderToSplitZip(const string &folderPath,
                              const string &zipOutputPath, int maxSizeMB,
                              const string &password, bool useParallel) {

  bool isEncrypted = !password.empty();

  // Validar tamaño máximo
  if (maxSizeMB <= 0) {
    cerr << "El tamaño máximo debe ser positivo" << endl;
    return false;
  }

  // -------------- PREPARACIÓN --------------

  // Tamaño máximo en bytes
  size_t maxSizeBytes = static_cast<size_t>(maxSizeMB) * 1024 * 1024;

  // Recolectar todos los archivos a comprimir (ignorando los que deben
  // excluirse)
  set<string> ignorePatterns = readIgnorePatterns(folderPath);

  // Configurar el número de hilos basado en el parámetro useParallel
  int originalMaxThreads = omp_get_max_threads();
  if (!useParallel) {
    omp_set_num_threads(1); // Forzar ejecución serial
    cout << "Modo serial activado (sin paralelismo)" << endl;
  } else {
    cout << "Modo paralelo activado con " << originalMaxThreads << " hilos"
         << endl;

    // Opcional: Ajustar la política de planificación de OpenMP para mejor
    // rendimiento
    omp_set_schedule(omp_sched_dynamic, 0);
  }

  // Guardar la ruta de los archivos a comprimir
  vector<filesystem::path> allFiles = collectFiles(folderPath, ignorePatterns);

  // Verificar si hay archivos para comprimir
  if (allFiles.empty()) {
    cerr << "No hay archivos para comprimir" << endl;
    // Restaurar configuración original de hilos
    if (!useParallel) {
      omp_set_num_threads(originalMaxThreads);
    }
    return false;
  }

  if (isEncrypted) {
    cout << "Modo encriptado activado" << endl;
    cout << "Hash de verificación: " << crypto.generatePasswordHash(password)
         << endl;
  }

  cout << "Total de archivos a comprimir: " << allFiles.size()
       << (useParallel ? " (usando paralelismo)" : " (modo serial)") << endl;

  // Crear la base para los nombres de archivo de salida
  filesystem::path baseOutputPath(zipOutputPath);
  string baseName = baseOutputPath.stem().string();
  string extension = baseOutputPath.extension().string();

  // Verificar que la extensión sea .zip
  if (extension.empty() || extension != ".zip") {
    extension = ".zip";
  }
  filesystem::path outputDir = baseOutputPath.parent_path();

  // Asegurarse de que el directorio de salida exista
  filesystem::create_directories(outputDir);

  // -------------- PROCESAMIENTO --------------

  bool overallSuccess = true;
  int part = 0;
  size_t fileIndex = 0;
  int totalParts = 0;
  int totalFragments = 0;

  // Calcular el número de partes necesarias
  totalParts = calculateTotalParts(allFiles, maxSizeBytes, maxSizeMB);

  // Procesar todos los archivos
  while (fileIndex < allFiles.size()) {
    // Verificar el próximo archivo
    uintmax_t nextFileSize = filesystem::file_size(allFiles[fileIndex]);
    string relativePath =
        filesystem::relative(allFiles[fileIndex], folderPath).string();

    // Si es un archivo grande, procesar con o sin paralelismo según el
    // parámetro useParallel
    if (nextFileSize > maxSizeBytes) {
      if (!useParallel) {
// Garantizar que las directivas paralelas en processLargeFile no tengan efecto
#pragma omp parallel num_threads(1)
        {
#pragma omp master
          {
            if (omp_get_num_threads() > 1) {
              cerr << "Error: El paralelismo no se desactivó correctamente"
                   << endl;
            }
          }
        }
      } else {
        // Configuración óptima para archivos grandes cuando se usa paralelismo
        int chunkSize = 0; // 0 significa que OpenMP decide automáticamente
        omp_set_schedule(omp_sched_dynamic, chunkSize);
      }

      // Procesar el archivo grande (la función interna respetará la
      // configuración de hilos)
      cout << "Procesando archivo grande"
           << (useParallel ? " con paralelismo..." : " en modo secuencial...")
           << endl;

      bool result = processLargeFile(allFiles[fileIndex], relativePath,
                                     folderPath, maxSizeBytes, baseName,
                                     extension, outputDir, part, totalParts,
                                     totalFragments, overallSuccess, password);
      fileIndex++;
      continue;
    }

    // Si es un archivo normal
    part++;
    bool result = processNormalFiles(
        allFiles, fileIndex, folderPath, maxSizeBytes, baseName, extension,
        outputDir, part, totalParts, overallSuccess, password);
  }

  cout << "\nCompresión" << (isEncrypted ? " encriptada" : "")
       << " completada en " << part << " partes";
  if (totalFragments > 0) {
    cout << " (incluyendo " << totalFragments
         << " fragmentos de archivos grandes)";
  }
  cout << "." << endl;

  // Restaurar configuración original de hilos al finalizar
  if (!useParallel) {
    omp_set_num_threads(originalMaxThreads);
  }

  return overallSuccess;
}