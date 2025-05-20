#include "compress.h"
#include <iostream>
#include <filesystem>
using namespace std;

void showHelp() {
    cout << "Uso: compressor -d [carpeta] -o [archivo_zip] [-p número_de_partes]" << endl;
    cout << "  -d : Directorio a comprimir (default: ./test)" << endl;
    cout << "  -o : Archivo ZIP de salida (default: ./output/archivo_comprimido.zip)" << endl;
    cout << "  -p : Número de partes en las que dividir la compresión (opcional)" << endl;
    cout << "       Si se omite, se creará un único archivo ZIP" << endl;
}

int main(int argc, char *argv[])
{
    if(argc < 2) {
        showHelp();
        return 0;
    }

    string sourceDir = "./test";
    string outputZip = "./output/archivo_comprimido.zip";
    int numParts = 1; // 0 significa sin división

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
        else if (string(argv[i]) == "-p" && i + 1 < argc)
        {
            try {
                numParts = stoi(argv[i + 1]);
                if (numParts <= 0) {
                    cerr << "Error: El número de partes debe ser positivo" << endl;
                    return 1;
                }
            } catch (const exception& e) {
                cerr << "Error al interpretar el número de partes: " << e.what() << endl;
                return 1;
            }
        }
        else if (string(argv[i]) == "-h" || string(argv[i]) == "--help")
        {
            showHelp();
            return 0;
        }
    }

    bool success;
    
    if (numParts > 0) {
        cout << "Comprimiendo en " << numParts << " partes..." << endl;
        success = compressFolderToSplitZip(sourceDir, outputZip, numParts);
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