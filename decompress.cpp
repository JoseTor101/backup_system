#include "decompress.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <zip.h>
#include <cstring>
#include <regex>
#include <algorithm>
#include <set>

using namespace std;

// Declaraciones anticipadas
bool reconstructFragmentedFile(const string &outputPath, 
                             vector<pair<string, zip_t*>> &archives,
                             const string &fragmentBaseName,
                             int totalFragments,
                             const string &originalPath);

// Parsea un archivo .info y extrae la información
PartInfo parseInfoFile(const string& infoContent) {
    PartInfo info;
    istringstream stream(infoContent);
    string line;
    
    // Leer la primera línea para obtener el número total de partes
    if (getline(stream, line)) {
        try {
            info.totalParts = stoi(line);
        } catch (...) {
            cerr << "Error al parsear el número total de partes: " << line << endl;
            info.totalParts = 0; // Error en el formato
        }
    }
    
    // Leer la segunda línea para obtener el número de esta parte
    if (getline(stream, line)) {
        try {
            info.partNumber = stoi(line);
        } catch (...) {
            cerr << "Error al parsear el número de parte: " << line << endl;
            info.partNumber = 0; // Error en el formato
        }
    }
    
    // Leer el mapeo de rutas (incluyendo fragmentos)
    while (getline(stream, line)) {
        if (line.empty()) continue;
        
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
                    
                    info.fragments.push_back(make_tuple(zipPath, remaining, fragNum, totalFrags));
                    // También lo agregamos como archivo normal para que aparezca en filePathMapping
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

// Extrae un archivo específico de un ZIP a la ruta destino
bool extractFileFromZip(zip_t* archive, const string& zipPath, const string& outputPath) {
    // Encontrar el archivo en el ZIP
    zip_int64_t index = zip_name_locate(archive, zipPath.c_str(), 0);
    if (index < 0) {
        cerr << "No se encuentra el archivo " << zipPath << " en el ZIP" << endl;
        return false;
    }
    
    // Abrir el archivo dentro del ZIP
    zip_file_t* zf = zip_fopen_index(archive, index, 0);
    if (!zf) {
        cerr << "No se puede abrir el archivo " << zipPath << " dentro del ZIP" << endl;
        return false;
    }
    
    // Obtener información del archivo
    zip_stat_t stat;
    if (zip_stat_index(archive, index, 0, &stat) < 0) {
        cerr << "No se puede obtener información del archivo " << zipPath << endl;
        zip_fclose(zf);
        return false;
    }
    
    // Crear directorio destino si no existe
    filesystem::path outputFile(outputPath);
    filesystem::create_directories(outputFile.parent_path());
    
    // Abrir archivo destino para escritura
    ofstream outFile(outputPath, ios::binary);
    if (!outFile) {
        cerr << "No se puede crear el archivo destino " << outputPath << endl;
        zip_fclose(zf);
        return false;
    }
    
    // Leer el contenido y escribirlo en el archivo destino
    const int bufferSize = 8192;
    char buffer[bufferSize];
    zip_int64_t bytesRead;
    while ((bytesRead = zip_fread(zf, buffer, bufferSize)) > 0) {
        outFile.write(buffer, bytesRead);
    }
    
    // Cerrar archivos
    outFile.close();
    zip_fclose(zf);
    
    cout << "    Extraído: " << outputPath << " (" << stat.size << " bytes)" << endl;
    return true;
}

// Leer el contenido de un archivo de texto desde un ZIP
string readTextFileFromZip(zip_t* archive, const string& zipPath) {
    zip_int64_t index = zip_name_locate(archive, zipPath.c_str(), 0);
    if (index < 0) {
        cerr << "No se encuentra el archivo " << zipPath << " en el ZIP" << endl;
        return "";
    }
    
    zip_file_t* zf = zip_fopen_index(archive, index, 0);
    if (!zf) {
        cerr << "No se puede abrir el archivo " << zipPath << " dentro del ZIP" << endl;
        return "";
    }
    
    zip_stat_t stat;
    if (zip_stat_index(archive, index, 0, &stat) < 0) {
        cerr << "No se puede obtener información del archivo " << zipPath << endl;
        zip_fclose(zf);
        return "";
    }
    
    vector<char> buffer(stat.size + 1, 0);
    zip_int64_t bytesRead = zip_fread(zf, buffer.data(), stat.size);
    zip_fclose(zf);
    
    if (bytesRead < 0) {
        cerr << "Error al leer el archivo " << zipPath << endl;
        return "";
    }
    
    return string(buffer.data(), bytesRead);
}


// Función principal para descomprimir partes
bool decompressParts(const string &folderPath, const string &outputPath) {
    // Buscar todos los archivos ZIP en el directorio especificado
    vector<filesystem::path> zipFiles;
    for (const auto& entry : filesystem::directory_iterator(folderPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".zip") {
            zipFiles.push_back(entry.path());
        }
    }
    
    if (zipFiles.empty()) {
        cerr << "No se encontraron archivos ZIP en " << folderPath << endl;
        return false;
    }
    
    cout << "Se encontraron " << zipFiles.size() << " archivos ZIP para descomprimir" << endl;
    
    // Asegurar que el directorio de salida exista
    filesystem::create_directories(outputPath);
    
    // Mantener una lista de todos los archivos ZIP abiertos para buscar fragmentos
    vector<pair<string, zip_t*>> allArchives;
    map<string, vector<tuple<string, string, int, int>>> allFragments;
    
    // Primera pasada: recopilar información de todos los fragmentos
    for (const auto& zipFile : zipFiles) {
        int err = 0;
        zip_t* archive = zip_open(zipFile.string().c_str(), 0, &err);
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
            const char* name = zip_get_name(archive, i, 0);
            if (name && strstr(name, ".info") != nullptr) {
                infoFileName = name;
                break;
            }
        }
        
        if (infoFileName.empty()) {
            cerr << "No se encontró archivo .info en " << zipFile << endl;
            continue;
        }
        
        // Leer y parsear el archivo .info
        string infoContent = readTextFileFromZip(archive, infoFileName);
        if (infoContent.empty()) {
            cerr << "No se pudo leer el archivo .info en " << zipFile << endl;
            continue;
        }
        
        PartInfo info = parseInfoFile(infoContent);
        cout << "  Parte " << info.partNumber << " de " << info.totalParts 
                  << " con " << info.filePathMapping.size() << " archivos" << endl;
        
        // Registrar todos los fragmentos encontrados
        for (const auto& [zipPath, originalPath, fragNum, totalFrags] : info.fragments) {
            string baseName = zipPath.substr(0, zipPath.find(".fragment"));
            
            if (allFragments.find(baseName) == allFragments.end()) {
                allFragments[baseName] = vector<tuple<string, string, int, int>>();
            }
            
            allFragments[baseName].push_back(make_tuple(zipPath, originalPath, fragNum, totalFrags));
        }
    }
    
    // Segunda pasada: procesar archivos normales
    for (const auto& [zipPath, archive] : allArchives) {
        cout << "Procesando " << zipPath << "..." << endl;
        
        // Buscar el archivo .info dentro del ZIP
        string infoFileName;
        zip_int64_t numEntries = zip_get_num_entries(archive, 0);
        for (zip_int64_t i = 0; i < numEntries; i++) {
            const char* name = zip_get_name(archive, i, 0);
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
        string infoContent = readTextFileFromZip(archive, infoFileName);
        if (infoContent.empty()) {
            cerr << "No se pudo leer el archivo .info en " << zipPath << endl;
            continue;
        }
        
        PartInfo info = parseInfoFile(infoContent);
        
        // Extraer archivos normales (no fragmentos)
        for (const auto& [zipPath, originalPath] : info.filePathMapping) {
            // Saltar fragmentos y archivos de información
            if (zipPath.find(".fragment") != string::npos || zipPath.find(".info") != string::npos) {
                continue;
            }
            
            // Construir la ruta de salida manteniendo la estructura de carpetas
            filesystem::path destPath = filesystem::path(outputPath) / zipPath;
            
            cout << "  Extrayendo " << zipPath << " a " << destPath << endl;
            
            if (!extractFileFromZip(archive, zipPath, destPath.string())) {
                cerr << "  Error al extraer " << zipPath << endl;
            }
        }
    }
    
    // Tercera pasada: reconstruir archivos fragmentados
    for (const auto& [baseName, fragments] : allFragments) {
        // Obtener el número total de fragmentos del primer fragmento
        if (fragments.empty()) continue;
        
        const auto& [zipPath, originalPath, fragNum, totalFrags] = fragments[0];
        
        // Verificar que tenemos todos los fragmentos
        set<int> foundFragNumbers;
        for (const auto& [_, __, fragNum, ___] : fragments) {
            foundFragNumbers.insert(fragNum);
        }
        
        if (foundFragNumbers.size() != static_cast<size_t>(totalFrags)) {
            cerr << "¡Advertencia! No se encontraron todos los fragmentos para " 
                      << baseName << ". Encontrados: " << foundFragNumbers.size() 
                      << " de " << totalFrags << endl;
            continue;
        }
        
        cout << "Reconstruyendo archivo fragmentado: " << baseName << endl;
        
        // Ruta de salida para el archivo reconstruido (mantener estructura de carpetas)
        filesystem::path outputFilePath = filesystem::path(outputPath) / baseName;
        
        // Asegurarse que el directorio existe
        filesystem::create_directories(outputFilePath.parent_path());
        
        ofstream outFile(outputFilePath, ios::binary);
        
        if (!outFile) {
            cerr << "No se pudo crear el archivo reconstruido: " << outputFilePath << endl;
            continue;
        }
        
        bool reconstructionSuccess = true;
        
        // Ordenar fragmentos por número
        vector<tuple<string, string, int, int>> sortedFragments = fragments;
        sort(sortedFragments.begin(), sortedFragments.end(), 
            [](const auto& a, const auto& b) {
                return get<2>(a) < get<2>(b);
            });
        
        // Procesar cada fragmento en orden
        for (const auto& [fragZipPath, fragOrigPath, fragNumber, _] : sortedFragments) {
            bool fragFound = false;
            
            // Buscar en qué archivo ZIP está este fragmento
            for (const auto& [archivePath, archive] : allArchives) {
                zip_int64_t index = zip_name_locate(archive, fragZipPath.c_str(), 0);
                if (index >= 0) {
                    // Encontramos el fragmento, extraerlo
                    zip_file_t* zf = zip_fopen_index(archive, index, 0);
                    if (!zf) {
                        cerr << "Error al abrir fragmento: " << fragZipPath << endl;
                        reconstructionSuccess = false;
                        break;
                    }
                    
                    zip_stat_t stat;
                    if (zip_stat_index(archive, index, 0, &stat) < 0) {
                        cerr << "Error al obtener información de fragmento: " << fragZipPath << endl;
                        zip_fclose(zf);
                        reconstructionSuccess = false;
                        break;
                    }
                    
                    // Copiar contenido del fragmento al archivo de salida
                    const int bufferSize = 8192;
                    char buffer[bufferSize];
                    zip_int64_t bytesRead;
                    
                    while ((bytesRead = zip_fread(zf, buffer, bufferSize)) > 0) {
                        outFile.write(buffer, bytesRead);
                        if (!outFile) {
                            cerr << "Error al escribir fragmento al archivo de salida" << endl;
                            reconstructionSuccess = false;
                            break;
                        }
                    }
                    
                    zip_fclose(zf);
                    fragFound = true;
                    cout << "  Procesado fragmento " << fragNumber << " de " << totalFrags 
                              << " (" << (stat.size / 1024) << "KB)" << endl;
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
            cout << "Archivo reconstruido correctamente: " << outputFilePath 
                      << " (" << (filesystem::file_size(outputFilePath) / 1024 / 1024) 
                      << "MB)" << endl;
        } else {
            cerr << "Error al reconstruir archivo fragmentado: " << baseName << endl;
        }
    }
    
    // Cerrar todos los archivos ZIP al finalizar
    for (auto& [path, archive] : allArchives) {
        zip_close(archive);
    }
    
    cout << "Descompresión completada en " << outputPath << endl;
    return true;
}

int main(int argc, char* argv[]) {
    string inputFolder = "./output";
    string outputFolder = "./extracted";
    
    if (argc > 1) inputFolder = argv[1];
    if (argc > 2) outputFolder = argv[2];
    
    cout << "Descomprimiendo archivos de " << inputFolder << " a " << outputFolder << endl;
    
    if (decompressParts(inputFolder, outputFolder)) {
        cout << "Operación completada con éxito." << endl;
        return 0;
    } else {
        cerr << "Error durante la operación." << endl;
        return 1;
    }
}