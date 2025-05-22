#include "compress.h"
#include <iostream>
#include <filesystem>
using namespace std;

void showHelp(int maxSizeMB = 50) {
    cout << "Uso: compressor -d [carpeta] -o [archivo_zip] [-s tamaño_MB] [-e contraseña]" << endl;
    cout << "  -d : Directorio a comprimir (default: ./test)" << endl;
    cout << "  -o : Archivo ZIP de salida (default: ./output/archivo_comprimido.zip)" << endl;
    cout << "  -s : Tamaño máximo por fragmento (en MB, default: " << maxSizeMB << ")" << endl;
    cout << "  -e : Contraseña para encriptado (opcional)" << endl;
    cout << "  -h : Mostrar esta ayuda" << endl;
}

int main(int argc, char *argv[])
{
    int maxSizeMB=50; // Tamaño máximo por fragmento en MB

    if(argc < 2) {
        showHelp(maxSizeMB);
        return 0;
    }

    string sourceDir = "./test";
    string outputZip = "./output/archivo_comprimido.zip";
    string encryptPassword = "";

    for(int i = 0; i < argc; i++)
    {
        if (string(argv[i]) == "-d" && i + 1 < argc)
        {
            sourceDir = argv[i + 1];
        }
        else if (string(argv[i]) == "-o" && i + 1 < argc)
        {
            outputZip = argv[i + 1];
        }
        else if (string(argv[i]) == "-s" && i + 1 < argc)
        {
            try {
                maxSizeMB = stoi(argv[i + 1]);
                if (maxSizeMB <= 0) {
                    cerr << "Error: El número de partes debe ser positivo" << endl;
                    return 1;
                }
            } catch (const exception& e) {
                cerr << "Error al interpretar el número de partes: " << e.what() << endl;
                return 1;
            }
        }
        else if (string(argv[i]) == "-e" && i + 1 < argc)
        {
            encryptPassword = argv[i + 1];
            cout << "Modo encriptado habilitado" << endl;
        }
        else if (string(argv[i]) == "-h" || string(argv[i]) == "--help")
        {
            showHelp();
            return 0;
        }
    }

    bool success;
    
    if (!encryptPassword.empty()) {
        cout << "Comprimiendo con encriptado..." << endl;
        success = compressFolderToSplitZipEncrypted(sourceDir, outputZip, maxSizeMB, encryptPassword);
    } else {
        success = compressFolderToSplitZip(sourceDir, outputZip, maxSizeMB);
    }
    if (success)
    {
        cout << "¡Compresión exitosa!" << endl;
    }
    else
    {
        cerr << "Error en la compresión." << endl;
    }

    return success ? 0 : 1;
}