#include <iostream>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

json loadCredentials() {
    return json::parse(readFile("credentials.json"))["installed"];
}

json loadTokens() {
    std::ifstream f("token.json");
    if (!f.good()) return nullptr;
    return json::parse(readFile("token.json"));
}

void saveTokens(const json& tokens) {
    writeFile("token.json", tokens.dump(4));
}

json getTokensFromAuthCode(const std::string& client_id, const std::string& client_secret, const std::string& code) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        std::string data = "code=" + code +
                           "&client_id=" + client_id +
                           "&client_secret=" + client_secret +
                           "&redirect_uri=urn:ietf:wg:oauth:2.0:oob" +
                           "&grant_type=authorization_code";

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "Token request failed: " << curl_easy_strerror(res) << "\n";

        curl_easy_cleanup(curl);
    }

    return json::parse(response);
}

std::string getAccessToken(json& tokens, const std::string& client_id, const std::string& client_secret) {
    if (!tokens.contains("refresh_token")) return "";

    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        std::string data = "client_id=" + client_id +
                           "&client_secret=" + client_secret +
                           "&refresh_token=" + tokens["refresh_token"].get<std::string>() +
                           "&grant_type=refresh_token";

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "Refresh token failed: " << curl_easy_strerror(res) << "\n";

        curl_easy_cleanup(curl);
    }

    json refreshed = json::parse(response);
    tokens["access_token"] = refreshed["access_token"];
    saveTokens(tokens);

    return tokens["access_token"];
}

int uploadFile(const std::string& filepath, const std::string& filename, const std::string& access_token) {
    CURL* curl;
    CURLcode res;
    struct curl_httppost* formpost = NULL;
    struct curl_httppost* lastptr = NULL;
    struct curl_slist* headers = NULL;

    std::string fileContent = readFile(filepath);

    std::string metadata = R"({"name":")" + filename + R"("})";

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        curl_formadd(&formpost, &lastptr,
                     CURLFORM_COPYNAME, "metadata",
                     CURLFORM_COPYCONTENTS, metadata.c_str(),
                     CURLFORM_CONTENTTYPE, "application/json",
                     CURLFORM_END);

        curl_formadd(&formpost, &lastptr,
                     CURLFORM_COPYNAME, "file",
                     CURLFORM_BUFFER, filename.c_str(),
                     CURLFORM_BUFFERPTR, fileContent.c_str(),
                     CURLFORM_BUFFERLENGTH, fileContent.size(),
                     CURLFORM_CONTENTTYPE, "application/octet-stream",
                     CURLFORM_END);

        std::string authHeader = "Authorization: Bearer " + access_token;
        headers = curl_slist_append(headers, authHeader.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart");
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "Upload failed: " << curl_easy_strerror(res) << "\n";
        else
            std::cout << "\n✅ Upload complete.\n";

        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        curl_slist_free_all(headers);
    }

    curl_global_cleanup();
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./drive_uploader <file_path>\n";
        return 1;
    }

    std::string filepath = argv[1];
    std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);

    json creds = loadCredentials();
    json tokens = loadTokens();

    std::string access_token;

    if (tokens.is_null()) {
        std::string auth_url = "https://accounts.google.com/o/oauth2/v2/auth"
                               "?client_id=" + creds["client_id"].get<std::string>() +
                               "&redirect_uri=urn:ietf:wg:oauth:2.0:oob"
                               "&response_type=code"
                               "&scope=https://www.googleapis.com/auth/drive.file";

        std::cout << "🔗 Open this URL in your browser:\n" << auth_url << "\n\n";
        std::cout << "Paste the authorization code here: ";
        std::string code;
        std::cin >> code;

        tokens = getTokensFromAuthCode(creds["client_id"], creds["client_secret"], code);
        saveTokens(tokens);
        access_token = tokens["access_token"];
    } else {
        access_token = getAccessToken(tokens, creds["client_id"], creds["client_secret"]);
    }

    uploadFile(filepath, filename, access_token);

    return 0;
}
