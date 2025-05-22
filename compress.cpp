#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <zip.h>
#include <memory>
#include <cstring>
#include "crypto.h"

using namespace std;

static SimpleCrypto crypto;

// Función para verificar si un archivo debe ser ignorado según los patrones
bool shouldIgnoreFile(const string &relativePath, const set<string> &ignorePatterns)
{
    // Si el archivo está vacío, no ignorarlo (caso edge)
    if (relativePath.empty())
    {
        return false;
    }

    // Comprobar cada patrón de ignore
    for (const auto &pattern : ignorePatterns)
    {
        // Si el patrón comienza con /, es una ruta relativa desde la raíz
        if (!pattern.empty() && pattern[0] == '/')
        {
            string cleanPattern = pattern.substr(1); // Quitar el / inicial
            // Verificar si la ruta comienza con el patrón (para ignorar directorios completos)
            if (relativePath == cleanPattern ||
                relativePath.find(cleanPattern + "/") == 0)
            {
                return true;
            }
        }
        // Patrón con comodín al final (ej: "*.txt")
        else if (!pattern.empty() && pattern.back() == '*')
        {
            string prefix = pattern.substr(0, pattern.length() - 1);
            if (relativePath.length() >= prefix.length() &&
                relativePath.substr(0, prefix.length()) == prefix)
            {
                return true;
            }
        }
        // Coincidencia exacta
        else if (relativePath == pattern)
        {
            return true;
        }
        // Verificar si es un archivo dentro del directorio especificado
        else if (relativePath.find(pattern + "/") == 0)
        {
            return true;
        }
    }

    return false;
}

// Función mejorada para añadir un buffer de memoria a un ZIP con opción de copiar
bool addBufferToZip(zip_t *archive, const char* buffer, size_t bufferSize, const string &zipPath, 
                   bool makeCopy = false, bool freeBuffer = true, bool *overallSuccess = nullptr, bool *opSuccess = nullptr) {
    
    // Opcionalmente crear una copia del buffer
    char* finalBuffer = (char*)buffer;
    if (makeCopy) {
        finalBuffer = new char[bufferSize];
        memcpy(finalBuffer, buffer, bufferSize);
    }
    
    // Crear una fuente ZIP con los datos
    zip_source_t* source = zip_source_buffer(archive, finalBuffer, bufferSize, freeBuffer ? 1 : 0);
    if (source == nullptr) {
        cerr << "Error al crear fuente ZIP para " << zipPath << ": " << zip_strerror(archive) << endl;
        if (makeCopy && freeBuffer) {
            delete[] finalBuffer;
        }
        if (overallSuccess) *overallSuccess = false;
        if (opSuccess) *opSuccess = false;
        return false;
    }
    
    // Agregar el buffer al ZIP
    zip_int64_t index = zip_file_add(archive, zipPath.c_str(), source, ZIP_FL_ENC_UTF_8);
    if (index < 0) {
        cerr << "Error al añadir " << zipPath << " al ZIP: " << zip_strerror(archive) << endl;
        zip_source_free(source);
        if (overallSuccess) *overallSuccess = false;
        if (opSuccess) *opSuccess = false;
        return false;
    }
    
    return true;
}

// Función para añadir un archivo de texto en memoria al ZIP (usado en el .info)
bool addTextToZip(zip_t *archive, const string &content, const string &zipPath) {
    // Crea una copia del contenido en un buffer nuevo
    // con explícita asignación de memoria para evitar problemas

    return addBufferToZip(archive, content.data(), content.size(), zipPath, true, true);
}

bool addFileToZip(zip_t *archive, const string &filePath, const string &zipPath)
{
    // Leer todo el archivo en memoria primero
    ifstream file(filePath, ios::binary);
    if (!file)
    {
        cerr << "No se pudo abrir el archivo: " << filePath << endl;
        return false;
    }

    // Obtener el tamaño del archivo
    file.seekg(0, ios::end);
    streamsize size = file.tellg();
    file.seekg(0, ios::beg);

    // Crear un buffer para el contenido del archivo
    auto fileContent = new char[size];
    if (!file.read(fileContent, size))
    {
        cerr << "Error al leer el archivo: " << filePath << endl;
        delete[] fileContent;
        return false;
    }

    return addBufferToZip(archive, fileContent, size, zipPath, false, true);
    
}

// Función para añadir un archivo grande en fragmentos
bool addLargeFileToZip(zip_t *archive, const string &filePath, const string &zipPath, 
                       size_t maxSizeBytes, int partNum, int totalParts, int *fragmentNum, bool &overallSuccess, bool &fragmentSuccess) {
    // Abrir el archivo
    ifstream file(filePath, ios::binary);
    if (!file) {
        cerr << "No se pudo abrir el archivo: " << filePath << endl;
        return false;
    }
    
    // Obtener el tamaño total del archivo
    file.seekg(0, ios::end);
    streamsize totalSize = file.tellg();
    file.seekg(0, ios::beg);
    
    // Calcular el número de fragmentos necesarios
    int fragments = static_cast<int>((totalSize + maxSizeBytes - 1) / maxSizeBytes);
    
    // Información para el archivo de control
    ostringstream fragmentInfo;
    fragmentInfo << "# FRAGMENT_INFO\n";
    fragmentInfo << "original_file: " << zipPath << "\n";
    fragmentInfo << "total_size: " << totalSize << "\n";
    fragmentInfo << "fragments: " << fragments << "\n";
    
    // Añadir archivo de información de fragmentación
    string fragmentInfoPath = zipPath + ".fragment_info";
    if (!addTextToZip(archive, fragmentInfo.str(), fragmentInfoPath)) {
        cerr << "Error al agregar información de fragmentación" << endl;
        return false;
    }
    
    cout << "  Fragmentando " << zipPath << " (" << (totalSize / 1024 / 1024) << "MB) "
              << "en " << fragments << " fragmentos" << endl;
    
    // Leer y comprimir el archivo en fragmentos
    char *buffer = new char[maxSizeBytes];
    bool success = true;
    int fragment = 0;
    
    while (file && fragment < fragments) {
        // Leer un fragmento del archivo
        streamsize bytesToRead = min(maxSizeBytes, static_cast<size_t>(totalSize - file.tellg()));
        if (!file.read(buffer, bytesToRead)) {
            cerr << "Error al leer fragmento del archivo: " << filePath << endl;
            success = false;
            break;
        }
        
        // Crear nombre para este fragmento
        string fragmentName = zipPath + ".fragment" + to_string(fragment + 1) + 
                              "_of_" + to_string(fragments);
        
        // Añadir fragmento al ZIP usando la función addBufferToZip
        if (!addBufferToZip(archive, buffer, bytesToRead, fragmentName, true, true, &overallSuccess, &fragmentSuccess)) {
            success = false;
            break;
        }

        cout << "    Fragmento " << (fragment + 1) << " de " << fragments 
              << " (" << (bytesToRead / 1024) << "KB)" << endl;
        
        fragment++;
        if (fragmentNum) (*fragmentNum)++;  // Incrementar el contador global solo si no es nullptr
    }
    
    delete[] buffer;
    
    if (!success) {
        cerr << "Error al fragmentar el archivo " << filePath << endl;
    } else {
        cout << "  Archivo fragmentado correctamente: " << zipPath << endl;
    }
    
    return success;
}


// Función para leer patrones a ignorar desde un archivo .ignore
set<string> readIgnorePatterns(const string &folderPath) {
    set<string> ignorePatterns;
    
    // Construir la ruta al archivo .ignore
    filesystem::path ignoreFile = filesystem::path(folderPath) / ".ignore";
    
    // Verificar si el archivo existe
    if (filesystem::exists(ignoreFile)) {
        cout << "Se encontró un archivo .ignore, omitiendo archivos listados." << endl;
        
        // Abrir el archivo para lectura
        ifstream ignoreStream(ignoreFile);
        string line;
        
        // Leer línea por línea
        while (getline(ignoreStream, line)) {
            // Eliminar espacios en blanco al principio
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            
            // Eliminar espacios en blanco al final
            if (line.find_last_not_of(" \t\r\n") != string::npos)
                line.erase(line.find_last_not_of(" \t\r\n") + 1);

            // Ignorar líneas vacías y comentarios
            if (!line.empty() && line[0] != '#') {
                ignorePatterns.insert(line);
                cout << "  - Ignorando: " << line << endl;
            }
        }
    }
    
    return ignorePatterns;
}

// Función para recolectar todos los archivos no ignorados en un directorio
vector<filesystem::path> collectFiles(const string &folderPath, const set<string> &ignorePatterns) {
    vector<filesystem::path> allFiles;
    
    // Recorrer recursivamente el directorio
    for (const auto &entry : filesystem::recursive_directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            // Obtener la ruta relativa al directorio base
            string relativePath = filesystem::relative(entry.path(), folderPath).string();
            
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
                allFiles.push_back(entry.path());
            }
        }
    }
    
    return allFiles;
}

// Función para calcular el número de partes necesarias según el tamaño de los archivos
int calculateTotalParts(const vector<filesystem::path> &allFiles, size_t maxSizeBytes, int maxSizeMB) {
    size_t estimatedSize = 0;
    int estimatedParts = 0;
    bool afterBigFile = false;  // Para rastrear si estamos después de un archivo grande
    
    for (const auto &filePath : allFiles) {
        uintmax_t fileSize = filesystem::file_size(filePath);
        
        if (fileSize > maxSizeBytes) {
            // Archivo grande
            int fragmentsNeeded = static_cast<int>((fileSize + maxSizeBytes - 1) / maxSizeBytes);
            estimatedParts += fragmentsNeeded;
            afterBigFile = true;  // Marcar que venimos de un archivo grande
            estimatedSize = 0;    // Resetear acumulador
        } else {
            // Archivo normal
            if (afterBigFile || estimatedSize + fileSize > maxSizeBytes) {
                // Si venimos de un archivo grande o excedemos el tamaño,
                // necesitamos una nueva parte
                estimatedParts++;
                estimatedSize = fileSize;
                afterBigFile = false;  // Ya no venimos de un archivo grande
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
    cout << "Dividiendo en aproximadamente " << totalParts 
              << " partes de hasta " << maxSizeMB << "MB cada una." << endl;
    
    return totalParts;
}

// Procesa un único archivo grande dividiéndolo en múltiples archivos ZIP
bool processLargeFile(const filesystem::path &filePath, const string &relativePath, const string &folderPath,
                     size_t maxSizeBytes, const string &baseName, const string &extension,
                     const filesystem::path &outputDir, int &part, int &totalParts, int &totalFragments,
                     bool &overallSuccess) {
    uintmax_t fileSize = filesystem::file_size(filePath);
    cout << "  Archivo grande detectado: " << relativePath 
          << " (" << (fileSize / 1024 / 1024) << "MB)" << endl;
    
    // Calcular fragmentos necesarios
    int fragmentsNeeded = static_cast<int>((fileSize + maxSizeBytes - 1) / maxSizeBytes);
    
    // Actualizar totalParts si es necesario
    if (fragmentsNeeded > totalParts) {
        totalParts = fragmentsNeeded;
    }
    
    cout << "  Dividiendo en " << fragmentsNeeded << " archivos ZIP..." << endl;
    
    // Procesar cada fragmento
    bool fragmentSuccess = true;
    ifstream largeFile(filePath.string(), ios::binary);
    if (!largeFile) {
        cerr << "  Error al abrir archivo grande: " << filePath << endl;
        return false;
    }
    
    char* buffer = new char[maxSizeBytes];
    
    for (int fragNum = 0; fragNum < fragmentsNeeded && fragmentSuccess; fragNum++) {
        part++;
        string partFileName = baseName + "_part" + to_string(part) +
                             "_of_" + to_string(totalParts) + extension;
        filesystem::path partPath = outputDir / partFileName;
        
        cout << "  Creando parte " << part << " para fragmento " 
              << (fragNum + 1) << " de " << fragmentsNeeded << endl;
        
        // Abrir archivo ZIP
        int zip_error = 0;
        zip_t *archive = zip_open(partPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
        if (!archive) {
            char errstr[128];
            zip_error_to_str(errstr, sizeof(errstr), zip_error, errno);
            cerr << "No se pudo crear el archivo ZIP: " << partPath << " - " << errstr << endl;
            overallSuccess = false;
            fragmentSuccess = false;
            continue;
        }
        
        // Leer un fragmento del archivo
        streamsize bytesToRead = min(maxSizeBytes, 
                           static_cast<size_t>(fileSize - (fragNum * maxSizeBytes)));
        
        largeFile.seekg(fragNum * maxSizeBytes);
        if (!largeFile.read(buffer, bytesToRead)) {
            cerr << "Error al leer fragmento del archivo: " << filePath << endl;
            overallSuccess = false;
            fragmentSuccess = false;
            break;
        }
        
        // Crear nombre para este fragmento
        string fragmentName = relativePath + ".fragment" + to_string(fragNum + 1) + 
                              "_of_" + to_string(fragmentsNeeded);
        
        // Añadir fragmento al ZIP
        char* fragmentBuffer = new char[bytesToRead];
        memcpy(fragmentBuffer, buffer, bytesToRead);

        if (!addBufferToZip(archive, fragmentBuffer, bytesToRead, fragmentName, false, true, &overallSuccess, &fragmentSuccess)) {
            break;
        }
        else {
            totalFragments++;
        }
        
        // Añadir archivo .info
        ostringstream fragInfoContent;
        fragInfoContent << totalParts << "\n";
        fragInfoContent << part << "\n";
        fragInfoContent << relativePath + ".fragment" + to_string(fragNum + 1) + 
                      "_of_" + to_string(fragmentsNeeded) + " | " + 
                      filePath.string() + "\n";
        
        if (!addTextToZip(archive, fragInfoContent.str(), "part_" + to_string(part) + ".info")) {
            cerr << "  Error al agregar archivo de información" << endl;
            overallSuccess = false;
            fragmentSuccess = false;
        } else {
            cout << "  Agregado: part_" << part << ".info (Información de fragmento)" << endl;
        }
        
        cout << "  Fragmento " << (fragNum + 1) << " de " << fragmentsNeeded 
              << " (" << (bytesToRead / 1024) << "KB) añadido a " << partPath.filename() << endl;
        
        // Cerrar el archivo ZIP
        if (zip_close(archive) < 0) {
            cerr << "Error al cerrar el archivo ZIP: " << partPath << endl;
            overallSuccess = false;
            fragmentSuccess = false;
        }
    }
    
    delete[] buffer;
    return fragmentSuccess;
}



// Procesa archivos normales agregándolos a un único archivo ZIP
bool processNormalFiles(vector<filesystem::path> &allFiles, size_t &fileIndex, const string &folderPath,
                      size_t maxSizeBytes, const string &baseName, const string &extension,
                      const filesystem::path &outputDir, int part, int totalParts,
                      bool &overallSuccess) {
    string partFileName = baseName + "_part" + to_string(part) +
                         "_of_" + to_string(totalParts) + extension;
    filesystem::path partPath = outputDir / partFileName;
    
    // Abrir el archivo ZIP para esta parte
    int zip_error = 0;
    zip_t *archive = zip_open(partPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!archive) {
        char errstr[128];
        zip_error_to_str(errstr, sizeof(errstr), zip_error, errno);
        cerr << "No se pudo crear el archivo ZIP: " << partPath << " - " << errstr << endl;
        overallSuccess = false;
        return false;
    }
    
    bool partSuccess = true;
    size_t currentSize = 0;
    size_t startIdx = fileIndex;
    
    // Crear archivo .info básico para esta parte
    ostringstream infoContent;
    infoContent << totalParts << "\n";
    infoContent << part << "\n";
    
    // Procesar archivos para esta parte
    while (fileIndex < allFiles.size()) {
        uintmax_t fileSize = filesystem::file_size(allFiles[fileIndex]);
        string relativePath = filesystem::relative(allFiles[fileIndex], folderPath).string();
        
        // Si es un archivo grande, salir del bucle
        if (fileSize > maxSizeBytes) {
            break;
        }
        
        // Si añadir este archivo excede el tamaño máximo, terminar esta parte
        if (currentSize > 0 && (currentSize + fileSize) > maxSizeBytes) {
            break;
        }
        
        // Agregar archivo al ZIP
        cout << "  Agregando: " << relativePath << " (" 
              << (fileSize / 1024) << "KB)" << endl;
        
        if (!addFileToZip(archive, allFiles[fileIndex].string(), relativePath)) {
            cerr << "  Error al agregar: " << allFiles[fileIndex] << endl;
            partSuccess = false;
            overallSuccess = false;
        } else {
            // Añadir información del archivo
            infoContent << relativePath << " | " << allFiles[fileIndex].string() << "\n";
            currentSize += fileSize;
        }
        
        fileIndex++;
    }
    
    // Añadir el archivo .info al ZIP
    string infoStr = infoContent.str();
    if (!addTextToZip(archive, infoStr, "part_" + to_string(part) + ".info")) {
        cerr << "  Error al agregar archivo de información" << endl;
        partSuccess = false;
        overallSuccess = false;
    } else {
        cout << "  Agregado: part_" << part << ".info (Información de rutas)" << endl;
    }

    // Cerrar el archivo ZIP
    if (zip_close(archive) < 0) {
        cerr << "Error al cerrar el archivo ZIP: " << partPath << endl;
        overallSuccess = false;
        partSuccess = false;
    }
    
    return partSuccess;
}



// Función principal para comprimir un directorio en múltiples archivos ZIP
bool compressFolderToSplitZip(const string &folderPath, const string &zipOutputPath, int maxSizeMB) {
    // Validar tamaño máximo
    if (maxSizeMB <= 0) {
        cerr << "El tamaño máximo debe ser positivo" << endl;
        return false;
    }

    // -------------- PREPARACIÓN --------------

    // Tamaño máximo en bytes
    size_t maxSizeBytes = static_cast<size_t>(maxSizeMB) * 1024 * 1024;
    
    // Recolectar todos los archivos a comprimir (ignorando los que deben excluirse)
    set<string> ignorePatterns = readIgnorePatterns(folderPath);

    // Guardar la ruta de los archivos a comprimir
    vector<filesystem::path> allFiles = collectFiles(folderPath, ignorePatterns);
    
    // Verificar si hay archivos para comprimir
    if (allFiles.empty()) {
        cerr << "No hay archivos para comprimir" << endl;
        return false;
    }
    
    cout << "Total de archivos a comprimir: " << allFiles.size() << endl;
    
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
        string relativePath = filesystem::relative(allFiles[fileIndex], folderPath).string();
        
        // Si es un archivo grande
        if (nextFileSize > maxSizeBytes) {
            bool result = processLargeFile(allFiles[fileIndex], relativePath, folderPath,
                                         maxSizeBytes, baseName, extension, outputDir,
                                         part, totalParts, totalFragments, overallSuccess);
            fileIndex++;
            continue;
        }
        
        // Si es un archivo normal
        part++;
        bool result = processNormalFiles(allFiles, fileIndex, folderPath, maxSizeBytes,
                                       baseName, extension, outputDir, part,
                                       totalParts, overallSuccess);
    }
    
    cout << "\nCompresión completada en " << part << " partes";
    if (totalFragments > 0) {
        cout << " (incluyendo " << totalFragments << " fragmentos de archivos grandes)";
    }
    cout << "." << endl;
    
    return overallSuccess;
}

bool addEncryptedBufferToZip(zip_t *archive, const char* buffer, size_t bufferSize, 
                           const string &zipPath, const string &password = "",
                           bool makeCopy = false, bool freeBuffer = true, 
                           bool *overallSuccess = nullptr, bool *opSuccess = nullptr) {
    
    char* finalBuffer = nullptr;
    size_t finalSize = bufferSize;
    
    if (!password.empty()) {
        // Encriptar el buffer
        auto encrypted = crypto.encrypt(reinterpret_cast<const unsigned char*>(buffer), bufferSize, password);
        finalSize = encrypted.size();
        finalBuffer = new char[finalSize];
        memcpy(finalBuffer, encrypted.data(), finalSize);
    } else {
        // Sin encriptado, usar buffer original
        if (makeCopy) {
            finalBuffer = new char[bufferSize];
            memcpy(finalBuffer, buffer, bufferSize);
        } else {
            finalBuffer = const_cast<char*>(buffer);
        }
    }
    
    // Crear fuente ZIP
    zip_source_t* source = zip_source_buffer(archive, finalBuffer, finalSize, 
                                           (!password.empty() || (makeCopy && freeBuffer)) ? 1 : 0);
    if (source == nullptr) {
        cerr << "Error al crear fuente ZIP para " << zipPath << ": " << zip_strerror(archive) << endl;
        if (!password.empty() || (makeCopy && freeBuffer)) {
            delete[] finalBuffer;
        }
        if (overallSuccess) *overallSuccess = false;
        if (opSuccess) *opSuccess = false;
        return false;
    }
    
    // Agregar al ZIP
    zip_int64_t index = zip_file_add(archive, zipPath.c_str(), source, ZIP_FL_ENC_UTF_8);
    if (index < 0) {
        cerr << "Error al añadir " << zipPath << " al ZIP: " << zip_strerror(archive) << endl;
        zip_source_free(source);
        if (overallSuccess) *overallSuccess = false;
        if (opSuccess) *opSuccess = false;
        return false;
    }
    
    return true;
}

// Función modificada para añadir archivo encriptado al ZIP
bool addEncryptedFileToZip(zip_t *archive, const string &filePath, const string &zipPath, const string &password = "") {
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

    bool result = addEncryptedBufferToZip(archive, fileContent, size, zipPath, password, false, true);
    return result;
}

// Función principal con encriptado
bool compressFolderToSplitZipEncrypted(const string &folderPath, const string &zipOutputPath, 
                                     int maxSizeMB, const string &password) {
    if (maxSizeMB <= 0) {
        cerr << "El tamaño máximo debe ser positivo" << endl;
        return false;
    }

    size_t maxSizeBytes = static_cast<size_t>(maxSizeMB) * 1024 * 1024;
    
    set<string> ignorePatterns = readIgnorePatterns(folderPath);
    vector<filesystem::path> allFiles = collectFiles(folderPath, ignorePatterns);
    
    if (allFiles.empty()) {
        cerr << "No hay archivos para comprimir" << endl;
        return false;
    }
    
    if (!password.empty()) {
        cout << "Modo encriptado activado" << endl;
        cout << "Hash de verificación: " << crypto.generatePasswordHash(password) << endl;
    }
    
    cout << "Total de archivos a comprimir: " << allFiles.size() << endl;
    
    filesystem::path baseOutputPath(zipOutputPath);
    string baseName = baseOutputPath.stem().string();
    string extension = baseOutputPath.extension().string();
    if (extension.empty() || extension != ".zip") {
        extension = ".zip";
    }
    filesystem::path outputDir = baseOutputPath.parent_path();
    filesystem::create_directories(outputDir);

    bool overallSuccess = true;
    int part = 0;
    size_t fileIndex = 0;
    int totalParts = calculateTotalParts(allFiles, maxSizeBytes, maxSizeMB);
    
    // Procesar archivos (usando funciones auxiliares modificadas)
    while (fileIndex < allFiles.size()) {
        uintmax_t nextFileSize = filesystem::file_size(allFiles[fileIndex]);
        
        if (nextFileSize > maxSizeBytes) {
            // Para archivos grandes, usaríamos processLargeFileEncrypted (no implementada aquí)
            fileIndex++;
            continue;
        }
        
        part++;
        string partFileName = baseName + "_part" + to_string(part) + "_of_" + to_string(totalParts) + extension;
        filesystem::path partPath = outputDir / partFileName;
        
        int zip_error = 0;
        zip_t *archive = zip_open(partPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
        if (!archive) {
            char errstr[128];
            zip_error_to_str(errstr, sizeof(errstr), zip_error, errno);
            cerr << "No se pudo crear el archivo ZIP: " << partPath << " - " << errstr << endl;
            overallSuccess = false;
            return false;
        }
        
        size_t currentSize = 0;
        ostringstream infoContent;
        infoContent << totalParts << "\n" << part << "\n";
        if (!password.empty()) {
            infoContent << "encrypted: " << crypto.generatePasswordHash(password) << "\n";
        }
        
        // Procesar archivos para esta parte
        while (fileIndex < allFiles.size()) {
            uintmax_t fileSize = filesystem::file_size(allFiles[fileIndex]);
            if (fileSize > maxSizeBytes || (currentSize > 0 && (currentSize + fileSize) > maxSizeBytes)) {
                break;
            }
            
            string relativePath = filesystem::relative(allFiles[fileIndex], folderPath).string();
            cout << "  Agregando" << (password.empty() ? "" : " (encriptado)") << ": " 
                 << relativePath << " (" << (fileSize / 1024) << "KB)" << endl;
            
            if (!addEncryptedFileToZip(archive, allFiles[fileIndex].string(), relativePath, password)) {
                cerr << "  Error al agregar: " << allFiles[fileIndex] << endl;
                overallSuccess = false;
            } else {
                infoContent << relativePath << " | " << allFiles[fileIndex].string() << "\n";
                currentSize += fileSize;
            }
            
            fileIndex++;
        }
        
        // Agregar archivo .info (también encriptado si hay contraseña)
        string infoStr = infoContent.str();
        if (!addEncryptedBufferToZip(archive, infoStr.data(), infoStr.size(), 
                                   "part_" + to_string(part) + ".info", password, true, true)) {
            cerr << "  Error al agregar archivo de información" << endl;
            overallSuccess = false;
        }
        
        if (zip_close(archive) < 0) {
            cerr << "Error al cerrar el archivo ZIP: " << partPath << endl;
            overallSuccess = false;
        }
    }
    
    cout << "\nCompresión" << (password.empty() ? "" : " encriptada") 
         << " completada en " << part << " partes." << endl;
    
    return overallSuccess;
}