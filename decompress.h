#ifndef DECOMPRESS_H
#define DECOMPRESS_H

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
#include <tuple>
#include <vector>
#include <zip.h>

/**
 * @struct PartInfo
 * @brief Estructura que almacena la información de una parte del archivo ZIP
 * fragmentado
 */
struct PartInfo {
  int totalParts = 0;
  int partNumber = 0;
  std::string encryptionHash = ""; // Hash for encrypted files
  std::map<std::string, std::string> filePathMapping; // zipPath -> originalPath
  std::vector<std::tuple<std::string, std::string, int, int>>
      fragments; // zipPath, originalPath, fragNum, totalFrags
};

/**
 * @brief Parsea un archivo .info y extrae la información
 * @param infoContent Contenido del archivo .info
 * @return Estructura PartInfo con la información parseada
 */
PartInfo parseInfoFile(const std::string &infoContent);

/**
 * @brief Extrae un archivo específico de un ZIP a la ruta destino con opción de
 * desencriptación
 * @param archive Puntero al archivo ZIP abierto
 * @param zipPath Ruta del archivo dentro del ZIP
 * @param outputPath Ruta donde se extraerá el archivo
 * @param password Contraseña para desencriptar el archivo (opcional)
 * @return true si la extracción fue exitosa, false en caso contrario
 */
bool extractFileFromZip(zip_t *archive, const std::string &zipPath,
                        const std::string &outputPath,
                        const std::string &password = "");

/**
 * @brief Lee el contenido de un archivo de texto desde un ZIP
 * @param archive Puntero al archivo ZIP abierto
 * @param zipPath Ruta del archivo dentro del ZIP
 * @return Contenido del archivo como string, o string vacío en caso de error
 */
std::string readTextFileFromZip(zip_t *archive, const std::string &zipPath);

/**
 * @brief Lee el contenido de un archivo de texto desde un ZIP con
 * desencriptación
 * @param archive Puntero al archivo ZIP abierto
 * @param zipPath Ruta del archivo dentro del ZIP
 * @param password Contraseña para desencriptar el archivo (opcional)
 * @return Contenido del archivo como string, o string vacío en caso de error
 */
std::string readTextFileFromZipWithDecryption(zip_t *archive,
                                              const std::string &zipPath,
                                              const std::string &password = "");

/**
 * @brief Reconstruye un archivo fragmentado a partir de sus partes
 * @param outputPath Ruta base de salida
 * @param archives Lista de todos los archivos ZIP abiertos para buscar
 * fragmentos
 * @param fragmentBaseName Nombre base del fragmento (sin el sufijo .fragmentX)
 * @param totalFragments Número total de fragmentos
 * @param originalPath Ruta original del archivo completo
 * @return true si la reconstrucción fue exitosa, false en caso contrario
 */
bool reconstructFragmentedFile(
    const std::string &outputPath,
    std::vector<std::pair<std::string, zip_t *>> &archives,
    const std::string &fragmentBaseName, int totalFragments,
    const std::string &originalPath);

/**
 * @brief Descomprime todos los archivos ZIP en el directorio especificado
 * @param folderPath Directorio donde se encuentran los archivos ZIP
 * @param outputPath Directorio donde se extraerán los archivos
 * @return true si la descompresión fue exitosa, false en caso contrario
 */
bool decompressParts(const std::string &folderPath,
                     const std::string &outputPath);

/**
 * @brief Descomprime todos los archivos ZIP en el directorio especificado con
 * desencriptación
 * @param folderPath Directorio donde se encuentran los archivos ZIP
 * @param outputPath Directorio donde se extraerán los archivos
 * @param password Contraseña para desencriptar los archivos (opcional)
 * @return true si la descompresión fue exitosa, false en caso contrario
 */
bool decompressPartsWithPassword(const std::string &folderPath,
                                 const std::string &outputPath,
                                 const std::string &password = "");

#endif // DECOMPRESS_H