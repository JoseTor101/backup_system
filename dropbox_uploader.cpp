#include "dropbox_uploader.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <curl/curl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <jsoncpp/json/json.h>
#include <mutex>
#include <sstream>

using namespace std;

// Variables globales para progreso
std::mutex progressMutex;
std::atomic<int> totalFilesUploaded{0};
std::atomic<int> totalFilesToUpload{0};

// Callback para recibir datos de respuesta HTTP
size_t WriteCallback(void *contents, size_t size, size_t nmemb, string *s) {
  s->append((char *)contents, size * nmemb);
  return size * nmemb;
}

// Callback para mostrar progreso de subida
int ProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t ultotal, curl_off_t ulnow) {
  if (ultotal == 0)
    return 0;

  static int lastPercent = 0;
  int percent = static_cast<int>((ulnow * 100) / ultotal);

  if (percent != lastPercent && percent % 5 == 0) {
    std::lock_guard<std::mutex> guard(progressMutex);
    lastPercent = percent;

    int barWidth = 30;
    int pos = barWidth * percent / 100;

    std::cout << "\r[";
    for (int i = 0; i < barWidth; ++i) {
      if (i < pos)
        std::cout << "=";
      else if (i == pos)
        std::cout << ">";
      else
        std::cout << " ";
    }
    std::cout << "] " << percent << "% " << std::flush;
  }

  return 0;
}

// Constructor
DropboxUploader::DropboxUploader() { curl_global_init(CURL_GLOBAL_ALL); }

// Destructor
DropboxUploader::~DropboxUploader() { curl_global_cleanup(); }

// Cargar credenciales
bool DropboxUploader::loadCredentials() {
  ifstream credFile("dropbox_credentials.json");
  if (!credFile.is_open()) {
    return false;
  }

  Json::Value root;
  Json::Reader reader;
  if (!reader.parse(credFile, root)) {
    return false;
  }

  authConfig.appKey = root["app_key"].asString();
  authConfig.appSecret = root["app_secret"].asString();
  authConfig.accessToken = root["access_token"].asString();
  authConfig.refreshToken = root["refresh_token"].asString();
  authConfig.tokenExpiry = root["token_expiry"].asString();

  return !authConfig.accessToken.empty();
}

// Guardar credenciales
bool DropboxUploader::saveCredentials() {
  ofstream credFile("dropbox_credentials.json");
  if (!credFile.is_open()) {
    return false;
  }

  Json::Value root;
  root["app_key"] = authConfig.appKey;
  root["app_secret"] = authConfig.appSecret;
  root["access_token"] = authConfig.accessToken;
  root["refresh_token"] = authConfig.refreshToken;
  root["token_expiry"] = authConfig.tokenExpiry;

  Json::StyledWriter writer;
  credFile << writer.write(root);

  return true;
}

// Verificar si el token ha expirado
bool DropboxUploader::isTokenExpired() {
  // En Dropbox, los tokens de acceso de corta duración suelen durar 4 horas
  if (authConfig.accessToken.empty() || authConfig.tokenExpiry.empty()) {
    return true;
  }

  try {
    std::tm tm = {};
    std::istringstream ss(authConfig.tokenExpiry);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

    auto expiry = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    auto now = std::chrono::system_clock::now();

    return now > expiry;
  } catch (...) {
    return true;
  }
}

// Refrescar token
string DropboxUploader::refreshAccessToken() {
  // Dropbox tiene tokens permanentes para apps, a menos que uses tokens de
  // corta duración Este es un placeholder simplificado, ya que muchas
  // aplicaciones de Dropbox usan tokens de larga duración que no necesitan ser
  // refrescados
  return authConfig.accessToken;
}

// Inicializar
bool DropboxUploader::initialize() {
  cout << "Inicializando conexión a Dropbox..." << endl;

  // Intentar cargar credenciales
  if (!loadCredentials()) {
    cout << "No se encontraron credenciales para Dropbox." << endl;
    cout << "Por favor, sigue estos pasos:" << endl;
    cout << "1. Ve a https://www.dropbox.com/developers/apps" << endl;
    cout << "2. Crea una nueva app" << endl;
    cout << "3. Selecciona 'Scoped Access' y 'App folder'" << endl;
    cout << "4. Asigna un nombre único a la app" << endl;
    cout << "5. En la página de tu app, busca la sección 'OAuth 2'" << endl;

    cout << "\nIngresa App Key: ";
    getline(cin, authConfig.appKey);

    cout << "Ingresa App Secret: ";
    getline(cin, authConfig.appSecret);

    // Para facilitar la obtención del token de acceso
    cout << "\nVe a esta URL para autorizar la app:" << endl;
    cout << "https://www.dropbox.com/oauth2/authorize?client_id="
         << authConfig.appKey << "&response_type=code&token_access_type=offline"
         << endl;

    cout << "\nIngresa el código de autorización obtenido: ";
    string authCode;
    getline(cin, authCode);

    if (authCode.empty()) {
      cout << "No se proporcionó un código de autorización válido." << endl;
      return false;
    }

    // Intercambiar código por token de acceso
    CURL *curl = curl_easy_init();
    if (!curl) {
      cout << "Error inicializando CURL." << endl;
      return false;
    }

    string postFields = "code=" + authCode + "&grant_type=authorization_code" +
                        "&client_id=" + authConfig.appKey +
                        "&client_secret=" + authConfig.appSecret;

    string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://api.dropboxapi.com/oauth2/token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
      cout << "Error al obtener token de acceso: " << curl_easy_strerror(res)
           << endl;
      return false;
    }

    // Parsear respuesta
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(readBuffer, root)) {
      cout << "Error al parsear respuesta de Dropbox." << endl;
      return false;
    }

    if (root.isMember("error")) {
      cout << "Error de Dropbox: " << root["error"].asString() << endl;
      if (root.isMember("error_description")) {
        cout << "Descripción: " << root["error_description"].asString() << endl;
      }
      return false;
    }

    authConfig.accessToken = root["access_token"].asString();

    // Dropbox proporciona un refresh_token si lo solicitamos con
    // token_access_type=offline
    if (root.isMember("refresh_token")) {
      authConfig.refreshToken = root["refresh_token"].asString();
    }

    // Calcular expiración (generalmente 4 horas para tokens de corta duración)
    int expiresIn = 14400; // 4 horas por defecto
    if (root.isMember("expires_in")) {
      expiresIn = root["expires_in"].asInt();
    }

    auto expiry =
        std::chrono::system_clock::now() + std::chrono::seconds(expiresIn);
    auto expTime = std::chrono::system_clock::to_time_t(expiry);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&expTime), "%Y-%m-%d %H:%M:%S");
    authConfig.tokenExpiry = ss.str();

    // Guardar credenciales
    saveCredentials();

    cout << "✅ Token de acceso obtenido correctamente." << endl;
  } else {
    cout << "Credenciales cargadas correctamente." << endl;
  }

  return true;
}

// Crear una carpeta en Dropbox
bool DropboxUploader::createFolder(const string &folderPath) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    return false;
  }

  // Preparar JSON para la solicitud
  Json::Value root;
  root["path"] = "/" + folderPath;
  root["autorename"] = false;

  Json::FastWriter writer;
  string postData = writer.write(root);

  string readBuffer;
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  string authHeader = "Authorization: Bearer " + authConfig.accessToken;
  headers = curl_slist_append(headers, authHeader.c_str());

  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://api.dropboxapi.com/2/files/create_folder_v2");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    return false;
  }

  Json::Value response;
  Json::Reader reader;
  if (!reader.parse(readBuffer, response)) {
    return false;
  }

  // Verificar si hay error
  if (response.isMember("error")) {
    string errorTag = response["error"].asString();
    if (errorTag == "path/conflict") {
      // La carpeta ya existe, lo que está bien
      return true;
    }
    return false;
  }

  return true;
}

// Subir un archivo a Dropbox
DropboxUploadResponse DropboxUploader::uploadFile(const string &filePath,
                                                  const string &folderPath) {
  DropboxUploadResponse response;

  // Verificar que el archivo existe
  if (!filesystem::exists(filePath)) {
    response.error = "El archivo no existe: " + filePath;
    return response;
  }

  // Construir la ruta de destino
  string fileName = filesystem::path(filePath).filename().string();
  string dropboxPath = folderPath.empty() ? ("/" + fileName)
                                          : ("/" + folderPath + "/" + fileName);

  // Leer el archivo
  ifstream file(filePath, ios::binary | ios::ate);
  if (!file.is_open()) {
    response.error = "No se puede abrir el archivo: " + filePath;
    return response;
  }

  streamsize fileSize = file.tellg();
  file.seekg(0, ios::beg);

  vector<char> buffer(fileSize);
  if (!file.read(buffer.data(), fileSize)) {
    response.error = "Error al leer el archivo: " + filePath;
    return response;
  }

  // Mostrar información
  cout << "Subiendo " << fileName << " (" << (fileSize / 1024)
       << "KB) a Dropbox..." << endl;

  CURL *curl = curl_easy_init();
  if (!curl) {
    response.error = "Error al inicializar curl";
    return response;
  }

  // Preparar JSON para los argumentos
  Json::Value args;
  args["path"] = dropboxPath;
  args["mode"] = "overwrite";
  args["autorename"] = true;
  args["mute"] = false;
  args["strict_conflict"] = false;

  Json::FastWriter writer;
  string argsJson = writer.write(args);

  // Eliminar saltos de línea del JSON
  argsJson.erase(std::remove(argsJson.begin(), argsJson.end(), '\n'),
                 argsJson.end());

  struct curl_slist *headers = NULL;
  headers =
      curl_slist_append(headers, ("Dropbox-API-Arg: " + argsJson).c_str());
  headers =
      curl_slist_append(headers, "Content-Type: application/octet-stream");

  string authHeader = "Authorization: Bearer " + authConfig.accessToken;
  headers = curl_slist_append(headers, authHeader.c_str());

  string readBuffer;
  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://content.dropboxapi.com/2/files/upload");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, fileSize);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

  // Configurar barra de progreso
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  cout << endl; // Nueva línea después de la barra de progreso

  if (res != CURLE_OK) {
    response.error = curl_easy_strerror(res);
    return response;
  }

  // Parsear respuesta
  Json::Value root;
  Json::Reader reader;
  if (!reader.parse(readBuffer, root)) {
    response.error = "Error al parsear respuesta de Dropbox";
    return response;
  }

  if (root.isMember("id")) {
    response.fileId = root["id"].asString();
    response.path = root["path_display"].asString();

    // Obtener enlace compartido
    response.shareUrl = createSharedLink(response.path);
  } else {
    response.error = "Respuesta inesperada de Dropbox";
  }

  return response;
}

// Implementación alternativa (reemplazar la función completa)
string DropboxUploader::createSharedLink(const string &path) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    cerr << "Error inicializando CURL" << endl;
    return "";
  }

  // Usar la API más simple
  Json::Value args;
  args["path"] = path;
  args["short_url"] = false;

  Json::FastWriter writer;
  string argsJson = writer.write(args);

  // Eliminar saltos de línea del JSON
  argsJson.erase(::std::remove(argsJson.begin(), argsJson.end(), '\n'),
                 argsJson.end());

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  string authHeader = "Authorization: Bearer " + authConfig.accessToken;
  headers = curl_slist_append(headers, authHeader.c_str());

  string readBuffer;

  // API más simple y compatible
  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://api.dropboxapi.com/2/sharing/create_shared_link");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, argsJson.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  cout << "Respuesta de create_shared_link: " << readBuffer << endl;

  if (res != CURLE_OK) {
    // Si hay error, puede ser porque el enlace ya existe
    if (readBuffer.find("shared_link_already_exists") != string::npos) {
      cout << "El enlace ya existe, intentando obtenerlo..." << endl;

      // Intentar obtener el enlace existente con una petición separada
      CURL *curl2 = curl_easy_init();
      if (!curl2) {
        return "";
      }

      Json::Value listArgs;
      listArgs["path"] = path;

      string listArgsJson = Json::FastWriter().write(listArgs);
      listArgsJson.erase(
          ::std::remove(listArgsJson.begin(), listArgsJson.end(), '\n'),
          listArgsJson.end());

      struct curl_slist *headers2 = NULL;
      headers2 = curl_slist_append(headers2, "Content-Type: application/json");
      headers2 = curl_slist_append(headers2, authHeader.c_str());

      string listBuffer;
      curl_easy_setopt(
          curl2, CURLOPT_URL,
          "https://api.dropboxapi.com/2/sharing/list_shared_links");
      curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, headers2);
      curl_easy_setopt(curl2, CURLOPT_POSTFIELDS, listArgsJson.c_str());
      curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &listBuffer);
      curl_easy_setopt(curl2, CURLOPT_VERBOSE, 0L);

      res = curl_easy_perform(curl2);
      curl_slist_free_all(headers2);
      curl_easy_cleanup(curl2);

      if (res == CURLE_OK) {
        Json::Value listRoot;
        Json::Reader reader;
        if (reader.parse(listBuffer, listRoot) && listRoot.isMember("links") &&
            listRoot["links"].isArray() && listRoot["links"].size() > 0) {
          return listRoot["links"][0]["url"].asString();
        }
      }
    }
    return "";
  }

  Json::Value root;
  Json::Reader reader;
  if (!reader.parse(readBuffer, root)) {
    return "";
  }

  if (root.isMember("url")) {
    return root["url"].asString();
  }

  return "";
}

// Subir múltiples archivos a una carpeta
bool DropboxUploader::uploadFiles(const vector<string> &filePaths,
                                  const string &folderPath) {
  if (filePaths.empty()) {
    cout << "No hay archivos para subir." << endl;
    return true;
  }

  // Crear la carpeta si se especificó una
  string uploadFolder = folderPath;
  if (!folderPath.empty()) {
    if (!createFolder(folderPath)) {
      cerr << "❌ Error al crear la carpeta en Dropbox: " << folderPath << endl;
      // Continuar de todos modos, podría ser que la carpeta ya exista
    }
  } else {
    // Si no se especificó carpeta, crear una con timestamp
    auto now = chrono::system_clock::now();
    auto nowTime = chrono::system_clock::to_time_t(now);

    stringstream folderName;
    folderName << "Archivos_" << put_time(localtime(&nowTime), "%Y%m%d_%H%M%S");

    uploadFolder = folderName.str();
    if (!createFolder(uploadFolder)) {
      cerr << "❌ Error al crear la carpeta en Dropbox: " << uploadFolder
           << endl;
      return false;
    }

    cout << "📁 Carpeta creada en Dropbox: " << uploadFolder << endl;
  }

  bool overallSuccess = true;
  vector<DropboxUploadResponse> uploadResults;

  // Verificar que los archivos existen
  vector<string> validFilePaths;
  for (const auto &filePath : filePaths) {
    if (filesystem::exists(filePath)) {
      validFilePaths.push_back(filePath);
    } else {
      cerr << "⚠️ El archivo no existe y será ignorado: " << filePath
           << endl;
    }
  }

  if (validFilePaths.empty()) {
    cerr << "❌ No se encontraron archivos válidos para subir." << endl;
    return false;
  }

  cout << "\n🚀 Iniciando subida de " << validFilePaths.size()
       << " archivos a Dropbox..." << endl;

  // Configurar variables para seguimiento del progreso
  totalFilesToUpload = validFilePaths.size();
  totalFilesUploaded = 0;

  for (const auto &filePath : validFilePaths) {
    string fileName = filesystem::path(filePath).filename().string();
    cout << "📤 (" << (totalFilesUploaded.load() + 1) << "/"
         << totalFilesToUpload.load() << ") Subiendo: " << fileName << endl;

    auto response = uploadFile(filePath, uploadFolder);

    if (!response.error.empty()) {
      cerr << "  ❌ Error al subir " << fileName << ": " << response.error
           << endl;
      overallSuccess = false;
    } else {
      cout << "  ✅ Subido correctamente: " << response.shareUrl << endl;
      uploadResults.push_back(response);
    }

    totalFilesUploaded++;
  }

  // Generar archivo de enlaces
  if (!uploadResults.empty()) {
    ofstream linksFile("dropbox_links.txt");
    if (linksFile.is_open()) {
      linksFile << "╔══════════════════════════════════════════════════════════"
                   "════════╗"
                << endl;
      linksFile << "║  Enlaces de descarga de Dropbox                          "
                   "        ║"
                << endl;
      linksFile << "╚══════════════════════════════════════════════════════════"
                   "════════╝"
                << endl;
      linksFile << endl;

      for (size_t i = 0; i < uploadResults.size(); i++) {
        string fileName =
            filesystem::path(validFilePaths[i]).filename().string();
        linksFile << "📄 " << fileName << ":" << endl;
        linksFile << "   🔗 " << uploadResults[i].shareUrl << endl << endl;
      }

      linksFile << "Generado el: " << __DATE__ << " " << __TIME__ << endl;
      linksFile.close();

      cout << "\n📋 Enlaces guardados en: dropbox_links.txt" << endl;
    }
  }

  if (overallSuccess) {
    cout << "\n✨ Todos los archivos se subieron correctamente ✨" << endl;
  } else {
    cout << "\n⚠️ Algunos archivos no pudieron ser subidos. Revisa los "
            "mensajes anteriores."
         << endl;
  }

  return overallSuccess;
}

// Subir todos los archivos de una carpeta local
bool DropboxUploader::uploadFolderContents(const string &folderPath,
                                           bool onlyZipFiles) {
  // Verificar que la carpeta existe
  if (!filesystem::exists(folderPath) ||
      !filesystem::is_directory(folderPath)) {
    cerr << "❌ Error: La carpeta especificada no existe: " << folderPath
         << endl;
    return false;
  }

  // Recopilar los archivos a subir
  vector<string> filesToUpload;
  for (const auto &entry : filesystem::directory_iterator(folderPath)) {
    if (entry.is_regular_file()) {
      string extension = entry.path().extension().string();
      if (!onlyZipFiles || extension == ".zip") {
        filesToUpload.push_back(entry.path().string());
      }
    }
  }

  if (filesToUpload.empty()) {
    cout << "⚠️ No se encontraron archivos" << (onlyZipFiles ? " ZIP" : "")
         << " para subir en: " << folderPath << endl;
    return true;
  }

  // Crear carpeta en Dropbox con el nombre de la carpeta local más timestamp
  string folderName = filesystem::path(folderPath).filename().string();
  auto now = chrono::system_clock::now();
  auto nowTime = chrono::system_clock::to_time_t(now);

  stringstream dropboxFolderName;
  dropboxFolderName << folderName << "_"
                    << put_time(localtime(&nowTime), "%Y%m%d_%H%M%S");

  // Subir los archivos a esta carpeta
  return uploadFiles(filesToUpload, dropboxFolderName.str());
}

// Funciones de conveniencia para usar desde main.cpp
bool uploadFolderContents(const std::string &folderPath, bool onlyZipFiles,
                          UploadService service) {
  DropboxUploader uploader;
  if (!uploader.initialize()) {
    cerr << "❌ Error inicializando conexión a Dropbox." << endl;
    return false;
  }
  return uploader.uploadFolderContents(folderPath, onlyZipFiles);
}

bool uploadFileList(const std::vector<std::string> &filePaths,
                    UploadService service) {
  DropboxUploader uploader;
  if (!uploader.initialize()) {
    cerr << "❌ Error inicializando conexión a Dropbox." << endl;
    return false;
  }
  return uploader.uploadFiles(filePaths);
}