#ifndef COMPRESS_H
#define COMPRESS_H

#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include <zip.h>

using namespace std;

/**
 * Verifica si un archivo debe ser ignorado según los patrones de exclusión.
 *
 * @param relativePath La ruta relativa del archivo a verificar
 * @param ignorePatterns Conjunto de patrones para ignorar
 * @return true si el archivo debe ser ignorado, false en caso contrario
 */
bool shouldIgnoreFile(const std::string &relativePath,
                      const std::set<std::string> &ignorePatterns);

/**
 * Función para leer patrones a ignorar desde un archivo .ignore
 *
 * @param folderPath Ruta del directorio que contiene el archivo .ignore
 * @return Conjunto de patrones de archivos/directorios a ignorar
 */
set<string> readIgnorePatterns(const string &folderPath);

/**
 * Función para recolectar todos los archivos no ignorados en un directorio
 *
 * @param folderPath Ruta del directorio a analizar
 * @param ignorePatterns Conjunto de patrones para ignorar
 * @return Vector con las rutas de todos los archivos a comprimir
 */
vector<filesystem::path> collectFiles(const string &folderPath,
                                      const set<string> &ignorePatterns);

/**
 * Función mejorada para añadir un buffer de memoria a un ZIP con opción de
 * copiar
 *
 * @param archive Puntero al archivo ZIP abierto
 * @param buffer Puntero al buffer con el contenido a añadir
 * @param bufferSize Tamaño del buffer en bytes
 * @param zipPath Ruta dentro del ZIP donde se añadirá el archivo
 * @param makeCopy Si es true, crea una copia del buffer
 * @param freeBuffer Si es true, libera el buffer después de usarlo
 * @param overallSuccess Puntero a variable de éxito global (se modifica en caso
 * de error)
 * @param opSuccess Puntero a variable de éxito de operación (se modifica en
 * caso de error)
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool addBufferToZip(zip_t *archive, const char *buffer, size_t bufferSize,
                    const string &zipPath, bool makeCopy = false,
                    bool freeBuffer = true, bool *overallSuccess = nullptr,
                    bool *opSuccess = nullptr);

/**
 * Añade un buffer de memoria encriptado a un ZIP.
 *
 * @param archive Puntero al archivo ZIP abierto
 * @param buffer Puntero al buffer con el contenido a añadir
 * @param bufferSize Tamaño del buffer en bytes
 * @param zipPath Ruta dentro del ZIP donde se añadirá el archivo
 * @param password Contraseña para encriptación (si está vacía, no encripta)
 * @param makeCopy Si es true, crea una copia del buffer
 * @param freeBuffer Si es true, libera el buffer después de usarlo
 * @param overallSuccess Puntero a variable de éxito global (se modifica en caso
 * de error)
 * @param opSuccess Puntero a variable de éxito de operación (se modifica en
 * caso de error)
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool addEncryptedBufferToZip(zip_t *archive, const char *buffer,
                             size_t bufferSize, const string &zipPath,
                             const string &password = "", bool makeCopy = false,
                             bool freeBuffer = true,
                             bool *overallSuccess = nullptr,
                             bool *opSuccess = nullptr);

/**
 * Añade contenido de texto como un archivo al ZIP.
 *
 * @param archive Puntero al archivo ZIP abierto
 * @param content Contenido del texto a añadir
 * @param zipPath Ruta dentro del ZIP donde se añadirá el archivo
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool addTextToZip(zip_t *archive, const string &content, const string &zipPath);

/**
 * Añade un archivo al archivo ZIP.
 *
 * @param archive Puntero al archivo ZIP abierto
 * @param filePath Ruta del archivo a añadir
 * @param zipPath Ruta dentro del ZIP donde se añadirá el archivo
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool addFileToZip(zip_t *archive, const string &filePath,
                  const string &zipPath);

/**
 * Añade un archivo encriptado al archivo ZIP.
 *
 * @param archive Puntero al archivo ZIP abierto
 * @param filePath Ruta del archivo a añadir
 * @param zipPath Ruta dentro del ZIP donde se añadirá el archivo
 * @param password Contraseña para la encriptación
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool addEncryptedFileToZip(zip_t *archive, const string &filePath,
                           const string &zipPath, const string &password = "");

/**
 * Función para calcular el número de partes necesarias según el tamaño de los
 * archivos
 *
 * @param allFiles Vector con las rutas de todos los archivos a comprimir
 * @param maxSizeBytes Tamaño máximo de cada parte en bytes
 * @param maxSizeMB Tamaño máximo de cada parte en MB (para mensajes)
 * @return Número estimado de partes necesarias
 */
int calculateTotalParts(const vector<filesystem::path> &allFiles,
                        size_t maxSizeBytes, int maxSizeMB);

/**
 * Procesa un único archivo grande dividiéndolo en múltiples archivos ZIP
 *
 * @param filePath Ruta del archivo a fragmentar
 * @param relativePath Ruta relativa del archivo respecto al directorio base
 * @param folderPath Ruta del directorio base
 * @param maxSizeBytes Tamaño máximo de cada fragmento en bytes
 * @param baseName Nombre base para los archivos ZIP de salida
 * @param extension Extensión para los archivos ZIP de salida
 * @param outputDir Directorio de salida para los archivos ZIP
 * @param part Referencia al contador de partes actual (se incrementa por cada
 * parte creada)
 * @param totalParts Referencia al número total de partes (puede actualizarse)
 * @param totalFragments Referencia al contador de fragmentos total
 * @param overallSuccess Referencia a variable de éxito global
 * @param password Contraseña para encriptación (opcional)
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool processLargeFile(const filesystem::path &filePath,
                      const string &relativePath, const string &folderPath,
                      size_t maxSizeBytes, const string &baseName,
                      const string &extension,
                      const filesystem::path &outputDir, int &part,
                      int &totalParts, int &totalFragments,
                      bool &overallSuccess, const string &password = "");

/**
 * Procesa archivos normales agregándolos a un único archivo ZIP
 *
 * @param allFiles Vector con las rutas de todos los archivos a comprimir
 * @param fileIndex Referencia al índice actual en el vector de archivos (se
 * incrementa)
 * @param folderPath Ruta del directorio base
 * @param maxSizeBytes Tamaño máximo del archivo ZIP en bytes
 * @param baseName Nombre base para el archivo ZIP de salida
 * @param extension Extensión para el archivo ZIP de salida
 * @param outputDir Directorio de salida para el archivo ZIP
 * @param part Número de parte actual
 * @param totalParts Número total de partes
 * @param overallSuccess Referencia a variable de éxito global
 * @param password Contraseña para encriptación (opcional)
 * @return true si la operación tuvo éxito, false en caso contrario
 */
bool processNormalFiles(vector<filesystem::path> &allFiles, size_t &fileIndex,
                        const string &folderPath, size_t maxSizeBytes,
                        const string &baseName, const string &extension,
                        const filesystem::path &outputDir, int part,
                        int totalParts, bool &overallSuccess,
                        const string &password = "");

/**
 * Comprime un directorio completo en múltiples archivos ZIP.
 *
 * @param folderPath Ruta del directorio a comprimir
 * @param zipOutputPath Ruta base para los archivos ZIP de salida
 * @param maxSizeMB Tamaño máximo de cada archivo ZIP en MB
 * @param password Contraseña para encriptación (opcional)
 * @return true si la compresión tuvo éxito, false en caso contrario
 */
bool compressFolderToSplitZip(const string &folderPath,
                              const string &zipOutputPath, int maxSizeMB,
                              const string &password = "");

#endif // COMPRESS_H