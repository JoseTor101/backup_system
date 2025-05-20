#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <zip.h>
#include <memory>

// Función para verificar si un archivo debe ser ignorado según los patrones
bool shouldIgnoreFile(const std::string &relativePath, const std::set<std::string> &ignorePatterns)
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
            std::string cleanPattern = pattern.substr(1); // Quitar el / inicial
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
            std::string prefix = pattern.substr(0, pattern.length() - 1);
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

bool addFileToZip(zip_t *archive, const std::string &filePath, const std::string &zipPath)
{
    // Leer todo el archivo en memoria primero
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
    {
        std::cerr << "No se pudo abrir el archivo: " << filePath << std::endl;
        return false;
    }

    // Obtener el tamaño del archivo
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Crear un buffer para el contenido del archivo
    auto fileContent = new char[size];
    if (!file.read(fileContent, size))
    {
        std::cerr << "Error al leer el archivo: " << filePath << std::endl;
        delete[] fileContent;
        return false;
    }

    // Crear una fuente ZIP con los datos del archivo
    zip_source_t *source = zip_source_buffer(archive, fileContent, size, 1);
    if (source == nullptr)
    {
        std::cerr << "Error al crear fuente ZIP: " << zip_strerror(archive) << std::endl;
        delete[] fileContent;
        return false;
    }

    // Añadir el archivo al ZIP
    zip_int64_t index = zip_file_add(archive, zipPath.c_str(), source, ZIP_FL_ENC_UTF_8);
    if (index < 0)
    {
        std::cerr << "Error al añadir archivo al ZIP: " << zip_strerror(archive) << std::endl;
        zip_source_free(source);
        delete[] fileContent;
        return false;
    }

    return true;
}

bool compressFolderToSplitZip(const std::string &folderPath, const std::string &zipOutputPath, int numParts)
{
    // Validar número de partes
    if (numParts <= 0)
    {
        std::cerr << "El número de partes debe ser positivo" << std::endl;
        return false;
    }

    // Recolectar todos los archivos a comprimir (ignorando los que deben excluirse)
    std::vector<std::filesystem::path> allFiles;
    std::set<std::string> ignorePatterns;

    // Leer patrones a ignorar del archivo .ignore
    std::filesystem::path ignoreFile = std::filesystem::path(folderPath) / ".ignore";
    if (std::filesystem::exists(ignoreFile))
    {
        std::cout << "Se encontró un archivo .ignore, omitiendo archivos listados." << std::endl;
        std::ifstream ignoreStream(ignoreFile);
        std::string line;
        while (std::getline(ignoreStream, line))
        {
            // Eliminar espacios en blanco al principio y al final
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (!line.empty() && line[0] != '#')
            { // Ignorar líneas vacías y comentarios
                ignorePatterns.insert(line);
                std::cout << "  - Ignorando: " << line << std::endl;
            }
        }
    }

    // Recolectar todos los archivos (excluyendo los ignorados)
    for (const auto &entry : std::filesystem::recursive_directory_iterator(folderPath))
    {
        if (entry.is_regular_file())
        {
            std::string relativePath = std::filesystem::relative(entry.path(), folderPath).string();
            bool shouldIgnore = (entry.path().filename() == ".ignore") ||
                                shouldIgnoreFile(relativePath, ignorePatterns);

            if (!shouldIgnore)
            {
                allFiles.push_back(entry.path());
            }
        }
    }

    // Verificar si hay archivos para comprimir
    if (allFiles.empty())
    {
        std::cerr << "No hay archivos para comprimir" << std::endl;
        return false;
    }

    std::cout << "Total de archivos a comprimir: " << allFiles.size() << std::endl;

    // Calcular cuántos archivos por parte
    int filesPerPart = (allFiles.size() + numParts - 1) / numParts; // Redondeo hacia arriba
    std::cout << "Dividiendo en " << numParts << " partes, "
              << filesPerPart << " archivos por parte." << std::endl;

    // Crear la base para los nombres de archivo de salida
    std::filesystem::path baseOutputPath(zipOutputPath);
    std::string baseName = baseOutputPath.stem().string();
    std::string extension = baseOutputPath.extension().string();
    std::filesystem::path outputDir = baseOutputPath.parent_path();

    bool overallSuccess = true;

    // Procesar cada parte
    for (int part = 0; part < numParts; part++)
    {
        // Calcular índices de inicio y fin para esta parte
        int startIdx = part * filesPerPart;
        int endIdx = std::min((part + 1) * filesPerPart, static_cast<int>(allFiles.size()));

        if (startIdx >= allFiles.size())
        {
            break; // No más archivos para procesar
        }

        // Crear nombre para esta parte
        std::string partFileName = baseName + "_part" + std::to_string(part + 1) +
                                   "_of_" + std::to_string(numParts) + extension;
        std::filesystem::path partPath = outputDir / partFileName;

        std::cout << "\nCreando parte " << (part + 1) << " de " << numParts
                  << " (" << (endIdx - startIdx) << " archivos): "
                  << partPath.string() << std::endl;

        // Asegurarse de que el directorio de salida exista
        std::filesystem::create_directories(partPath.parent_path());

        // Eliminar el archivo ZIP si ya existe
        if (std::filesystem::exists(partPath))
        {
            std::filesystem::remove(partPath);
        }

        // Crear el archivo ZIP para esta parte
        int err = 0;
        zip_t *archive = zip_open(partPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        if (!archive)
        {
            char errstr[128];
            zip_error_to_str(errstr, sizeof(errstr), err, errno);
            std::cerr << "Error al crear archivo ZIP para la parte " << (part + 1)
                      << ": " << errstr << std::endl;
            overallSuccess = false;
            continue;
        }

        bool partSuccess = true;

        // Agregar archivos a esta parte
        for (int i = startIdx; i < endIdx; i++)
        {
            std::string relativePath = std::filesystem::relative(allFiles[i], folderPath).string();
            std::cout << "  Agregando: " << relativePath << std::endl;

            if (!addFileToZip(archive, allFiles[i].string(), relativePath))
            {
                std::cerr << "  Error al agregar: " << allFiles[i] << std::endl;
                partSuccess = false;
                overallSuccess = false;
            }
        }

        // Cerrar el archivo ZIP
        if (zip_close(archive) < 0)
        {
            std::cerr << "Error al cerrar el archivo ZIP para la parte " << (part + 1) << std::endl;
            partSuccess = false;
            overallSuccess = false;
        }
        else if (partSuccess)
        {
            std::cout << "  Parte " << (part + 1) << " creada correctamente: "
                      << partPath.string() << std::endl;
            std::cout << "  Tamaño del archivo: "
                      << std::filesystem::file_size(partPath) << " bytes" << std::endl;
        }
    }

    return overallSuccess;
}
