#ifndef DROPBOX_UPLOADER_H
#define DROPBOX_UPLOADER_H

#include <filesystem>
#include <string>
#include <vector>

// Estructura para la respuesta de la subida a Dropbox
struct DropboxUploadResponse {
  std::string fileId;
  std::string path;
  std::string shareUrl;
  std::string error;
};

// Estructura para la configuración de autenticación
struct DropboxAuthConfig {
  std::string appKey;
  std::string appSecret;
  std::string accessToken;
  std::string refreshToken;
  std::string tokenExpiry;
};

// Enumeración para mantener compatibilidad con el código existente
enum UploadService { DROPBOX };

// Clase para manejar la subida a Dropbox
class DropboxUploader {
private:
  DropboxAuthConfig authConfig;
  bool loadCredentials();
  bool saveCredentials();
  bool isTokenExpired();
  std::string refreshAccessToken();

  // Función interna para obtener URL compartida
  std::string createSharedLink(const std::string &path);

public:
  DropboxUploader();
  ~DropboxUploader();

  // Inicializar y verificar credenciales
  bool initialize();

  // Subir un archivo a Dropbox
  DropboxUploadResponse uploadFile(const std::string &filePath,
                                   const std::string &folderPath = "");

  // Crear una carpeta en Dropbox
  bool createFolder(const std::string &folderPath);

  // Subir múltiples archivos a una carpeta
  bool uploadFiles(const std::vector<std::string> &filePaths,
                   const std::string &folderPath = "");

  // Subir todos los archivos de una carpeta local
  bool uploadFolderContents(const std::string &folderPath,
                            bool onlyZipFiles = true);
};

// Funciones de conveniencia para usar directamente desde main.cpp
bool uploadFolderContents(const std::string &folderPath,
                          bool onlyZipFiles = true,
                          UploadService service = DROPBOX);
bool uploadFileList(const std::vector<std::string> &filePaths,
                    UploadService service = DROPBOX);

#endif // DROPBOX_UPLOADER_H