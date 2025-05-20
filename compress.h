#ifndef COMPRESS_H
#define COMPRESS_H

#include <string>
#include <set>
#include <zip.h>

/**
 * Verifica si un archivo debe ser ignorado según los patrones de exclusión.
 * 
 * @param relativePath La ruta relativa del archivo a verificar
 * @param ignorePatterns Conjunto de patrones para ignorar
 * @return true si el archivo debe ser ignorado, false en caso contrario
 */
bool shouldIgnoreFile(const std::string &relativePath, const std::set<std::string> &ignorePatterns);

/**
 * Añade un archivo al archivo ZIP.
 * 
 * @param archive Puntero al archivo ZIP abierto
 * @param filePath Ruta del archivo a añadir
 * @param zipPath Ruta dentro del ZIP donde se añadirá el archivo
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool addFileToZip(zip_t *archive, const std::string &filePath, const std::string &zipPath);


/**
 * Comprime un directorio completo en múltiples archivos ZIP.
 * 
 * @param folderPath Ruta del directorio a comprimir
 * @param zipOutputPath Ruta base para los archivos ZIP de salida
 * @param numParts Número de partes en las que se dividirá la compresión
 * @return true si la compresión tuvo éxito, false en caso contrario
 */
bool compressFolderToSplitZip(const std::string &folderPath, const std::string &zipOutputPath, int numParts);
#endif // COMPRESS_H