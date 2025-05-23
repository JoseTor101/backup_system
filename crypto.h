#ifndef CRYPTO_H
#define CRYPTO_H

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// Implementación simple de XOR cipher con clave expandida
class SimpleCrypto {
private:
  std::vector<unsigned char> expandKey(const std::string &password,
                                       size_t targetLength) {
    std::vector<unsigned char> key;
    key.reserve(targetLength);

    if (password.empty()) {
      // Clave por defecto si no se proporciona contraseña
      std::string defaultKey = "DefaultBackupKey2024!";
      for (size_t i = 0; i < targetLength; ++i) {
        key.push_back(defaultKey[i % defaultKey.length()] ^ (i & 0xFF));
      }
    } else {
      // Expandir la contraseña con transformaciones simples
      for (size_t i = 0; i < targetLength; ++i) {
        unsigned char base = password[i % password.length()];
        unsigned char modifier = (i / password.length()) & 0xFF;
        key.push_back(base ^ modifier ^ (i & 0xFF));
      }
    }

    return key;
  }

public:
  // Encriptar datos en memoria
  std::vector<unsigned char> encrypt(const unsigned char *data,
                                     size_t dataLength,
                                     const std::string &password = "") {
    std::vector<unsigned char> key = expandKey(password, dataLength);
    std::vector<unsigned char> encrypted(dataLength);

    for (size_t i = 0; i < dataLength; ++i) {
      encrypted[i] = data[i] ^ key[i];
    }

    return encrypted;
  }

  // Desencriptar datos en memoria
  std::vector<unsigned char> decrypt(const unsigned char *encryptedData,
                                     size_t dataLength,
                                     const std::string &password = "") {
    // XOR es simétrico, así que decrypt = encrypt
    return encrypt(encryptedData, dataLength, password);
  }

  // Encriptar string
  std::string encryptString(const std::string &plaintext,
                            const std::string &password = "") {
    auto encrypted =
        encrypt(reinterpret_cast<const unsigned char *>(plaintext.c_str()),
                plaintext.length(), password);
    return std::string(encrypted.begin(), encrypted.end());
  }

  // Desencriptar string
  std::string decryptString(const std::string &ciphertext,
                            const std::string &password = "") {
    auto decrypted =
        decrypt(reinterpret_cast<const unsigned char *>(ciphertext.c_str()),
                ciphertext.length(), password);
    return std::string(decrypted.begin(), decrypted.end());
  }

  // Generar hash simple de la contraseña para verificación
  std::string generatePasswordHash(const std::string &password) {
    std::hash<std::string> hasher;
    size_t hashValue = hasher(password + "BackupSalt2024");

    std::stringstream ss;
    ss << std::hex << hashValue;
    return ss.str();
  }
};

#endif // CRYPTO_H