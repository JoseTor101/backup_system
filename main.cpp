#include "compress.h"
#include "dropbox_uploader.h"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <omp.h>

using namespace std;
using namespace std::chrono;

struct PerformanceStats {
  double timeSerial;
  double timeParallel;
  size_t totalFiles;
  uintmax_t totalSize;
};

void showHelp(int maxSizeMB = 50) {
  cout << "Uso: compressor -d [carpeta] -o [archivo_zip] [-s tamaño_MB] [-e "
          "contraseña] [-p] [-u | -g]"
       << endl;
  cout << "  -d : Directorio a comprimir (default: ./test)" << endl;
  cout << "  -o : Archivo ZIP de salida (default: "
          "./output/archivo_comprimido.zip)"
       << endl;
  cout << "  -s : Tamaño máximo por fragmento (en MB, default: " << maxSizeMB
       << ")" << endl;
  cout << "  -e : Contraseña para encriptado (opcional)" << endl;
  cout << "  -p : Usar procesamiento paralelo (default: desactivado)" << endl;
  cout << "  -u : Subir archivos ZIP generados a Transfer.sh (default: "
          "desactivado)"
       << endl;
  cout << "  -g : Subir archivos ZIP generados a Google Drive (default: "
          "desactivado)"
       << endl;
  cout << "  -b : Ejecutar benchmark comparativo entre serial y paralelo"
       << endl;
  cout << "  -h : Mostrar esta ayuda" << endl;
}

// Función para calcular el tamaño total de los archivos
uintmax_t calculateTotalSize(const vector<filesystem::path> &files) {
  uintmax_t totalSize = 0;
  for (const auto &file : files) {
    totalSize += filesystem::file_size(file);
  }
  return totalSize;
}

// Función para mostrar tabla comparativa
void showPerformanceComparison(const PerformanceStats &stats) {
  cout << "\n╔═════════════════════════════════════════════════════════════════"
          "═══════════╗"
       << endl;
  cout << "║                      COMPARATIVA DE RENDIMIENTO                   "
          "        ║"
       << endl;
  cout << "╠════════════════════════════════╦═══════════════════╦══════════════"
          "═════════╣"
       << endl;
  cout << "║ Modo                          ║ Tiempo (segundos) ║ Archivos/s    "
          "         ║"
       << endl;
  cout << "╠════════════════════════════════╬═══════════════════╬══════════════"
          "═════════╣"
       << endl;
  cout << "║ Serial                        ║ " << fixed << setprecision(4)
       << setw(15) << stats.timeSerial << " ║ " << setw(19)
       << (stats.timeSerial > 0 ? stats.totalFiles / stats.timeSerial : 0)
       << " ║" << endl;
  cout << "║ Paralelo                      ║ " << fixed << setprecision(4)
       << setw(15) << stats.timeParallel << " ║ " << setw(19)
       << (stats.timeParallel > 0 ? stats.totalFiles / stats.timeParallel : 0)
       << " ║" << endl;
  cout << "╠════════════════════════════════╬═══════════════════╬══════════════"
          "═════════╣"
       << endl;

  double speedup = stats.timeSerial / stats.timeParallel;
  double efficiency = speedup / omp_get_max_threads() * 100.0;

  cout << "║ Aceleración (Speedup)         ║ " << fixed << setprecision(2)
       << setw(15) << speedup << " ║                   ║" << endl;
  cout << "║ Eficiencia (%)                ║ " << fixed << setprecision(2)
       << setw(15) << efficiency << " ║                   ║" << endl;
  cout << "╠════════════════════════════════╬═══════════════════╬══════════════"
          "═════════╣"
       << endl;
  cout << "║ Núcleos utilizados            ║ " << setw(15)
       << omp_get_max_threads() << " ║                   ║" << endl;
  cout << "║ Total archivos procesados     ║ " << setw(15) << stats.totalFiles
       << " ║                   ║" << endl;
  cout << "║ Tamaño total (MB)             ║ " << setw(15)
       << stats.totalSize / (1024 * 1024) << " ║                   ║" << endl;
  cout << "╚════════════════════════════════╩═══════════════════╩══════════════"
          "═════════╝"
       << endl;

  cout << "\nModo recomendado: " << (speedup > 1.1 ? "PARALELO" : "SERIAL")
       << (speedup > 1.1 ? " (más rápido en este sistema)"
                         : " (la sobrecarga del paralelismo no compensa)")
       << endl;
}

// Función para ejecutar benchmark
PerformanceStats runBenchmark(const string &sourceDir, const string &outputZip,
                              int maxSizeMB, const string &encryptPassword) {
  PerformanceStats stats;
  set<string> ignorePatterns = readIgnorePatterns(sourceDir);
  vector<filesystem::path> allFiles = collectFiles(sourceDir, ignorePatterns);

  stats.totalFiles = allFiles.size();
  stats.totalSize = calculateTotalSize(allFiles);

  cout << "\n▶ Ejecutando versión SERIAL para comparación..." << endl;

  // Modificar la ruta de salida para diferenciar entre serial y paralelo
  string serialOutput = outputZip;
  size_t dotPos = serialOutput.find_last_of('.');
  if (dotPos != string::npos) {
    serialOutput.insert(dotPos, "_serial");
  } else {
    serialOutput += "_serial";
  }

  // Medir tiempo de versión serial
  auto startSerial = high_resolution_clock::now();
  bool successSerial = compressFolderToSplitZip(
      sourceDir, serialOutput, maxSizeMB, encryptPassword, false);
  auto endSerial = high_resolution_clock::now();
  stats.timeSerial = duration<double>(endSerial - startSerial).count();

  cout << "\n▶ Ejecutando versión PARALELA para comparación..." << endl;

  // Modificar la ruta de salida para versión paralela
  string parallelOutput = outputZip;
  dotPos = parallelOutput.find_last_of('.');
  if (dotPos != string::npos) {
    parallelOutput.insert(dotPos, "_parallel");
  } else {
    parallelOutput += "_parallel";
  }

  // Medir tiempo de versión paralela
  auto startParallel = high_resolution_clock::now();
  bool successParallel = compressFolderToSplitZip(
      sourceDir, parallelOutput, maxSizeMB, encryptPassword, true);
  auto endParallel = high_resolution_clock::now();
  stats.timeParallel = duration<double>(endParallel - startParallel).count();

  return stats;
}

int main(int argc, char *argv[]) {
  int maxSizeMB = 50;            // Tamaño máximo por fragmento en MB
  bool useParallel = false;      // Por defecto se usa procesamiento serial
  bool runBenchmarkFlag = false; // Flag para ejecutar benchmark
  bool uploadFlag = false;       // Flag para subir archivos - Nueva variable
  bool uploadToDrive = false;    // Nueva bandera para Google Drive

  if (argc < 2) {
    showHelp(maxSizeMB);
    return 0;
  }

  string sourceDir = "./test";
  string outputZip = "./output/archivo_comprimido.zip";
  string encryptPassword = "";

  for (int i = 0; i < argc; i++) {
    if (string(argv[i]) == "-d" && i + 1 < argc) {
      sourceDir = argv[i + 1];
    } else if (string(argv[i]) == "-o" && i + 1 < argc) {
      outputZip = argv[i + 1];
    } else if (string(argv[i]) == "-s" && i + 1 < argc) {
      try {
        maxSizeMB = stoi(argv[i + 1]);
        if (maxSizeMB <= 0) {
          cerr << "Error: El número de partes debe ser positivo" << endl;
          return 1;
        }
      } catch (const exception &e) {
        cerr << "Error al interpretar el número de partes: " << e.what()
             << endl;
        return 1;
      }
    } else if (string(argv[i]) == "-e" && i + 1 < argc) {
      encryptPassword = argv[i + 1];
      cout << "Modo encriptado habilitado" << endl;
    } else if (string(argv[i]) == "-p") {
      useParallel = true;
      cout << "Modo paralelo habilitado con " << omp_get_max_threads()
           << " hilos" << endl;
    } else if (string(argv[i]) == "-u" || string(argv[i]) == "-g") {
      uploadFlag = true;
      cout << "Modo de subida habilitado: los archivos ZIP generados se "
              "subirán a Google Drive"
           << endl;
    } else if (string(argv[i]) == "-b") {
      runBenchmarkFlag = true;
      cout << "Modo benchmark activado: se ejecutarán versiones serial y "
              "paralela para comparar"
           << endl;
    } else if (string(argv[i]) == "-h" || string(argv[i]) == "--help") {
      showHelp();
      return 0;
    }
  }

  PerformanceStats stats = {0, 0, 0, 0};
  bool success = false;

  // Extraer la carpeta de salida desde la ruta del ZIP
  filesystem::path outputPath(outputZip);
  filesystem::path outputDir = outputPath.parent_path();

  if (runBenchmarkFlag) {
    // Ejecutar ambas versiones y mostrar comparativa
    stats = runBenchmark(sourceDir, outputZip, maxSizeMB, encryptPassword);
    showPerformanceComparison(stats);
    success = true; // Ambas versiones se ejecutaron

    // Si además se solicitó subir los archivos, intentar subir ambas versiones
    if (uploadFlag) {
      cout << "\n🔄 Iniciando proceso de subida de archivos ZIP generados..."
           << endl;

      // Subir los archivos de la versión serial y paralela
      string serialOutputDir =
          outputDir
              .string(); // La carpeta donde se guardaron los archivos seriales
      string parallelOutputDir =
          outputDir
              .string(); // La carpeta donde se guardaron los archivos paralelos

      cout << "\n📂 Subiendo archivos de la carpeta: " << outputDir.string()
           << endl;
      uploadFolderContents(outputDir.string(), true); // Solo archivos ZIP
    }
  } else {
    // Ejecutar solo la versión seleccionada
    cout << "Comprimiendo"
         << (!encryptPassword.empty() ? " con encriptado" : "")
         << (useParallel ? " (modo paralelo)..." : " (modo serial)...") << endl;

    // Medir tiempo
    auto start = high_resolution_clock::now();
    success = compressFolderToSplitZip(sourceDir, outputZip, maxSizeMB,
                                       encryptPassword, useParallel);
    auto end = high_resolution_clock::now();
    double time_taken = duration<double>(end - start).count();

    if (success) {
      cout << "¡Compresión exitosa en " << fixed << setprecision(2)
           << time_taken << " segundos!" << endl;

      // Si se activó la bandera de subida, iniciar el proceso
      if (uploadFlag) {
        cout << "\n🔄 Iniciando proceso de subida de archivos ZIP generados..."
             << endl;
        cout << "\n📂 Subiendo archivos de la carpeta: " << outputDir.string()
             << endl;
        uploadFolderContents(outputDir.string(), true);
      }
    } else {
      cerr << "Error en la compresión." << endl;
    }
  }

  return success ? 0 : 1;
}