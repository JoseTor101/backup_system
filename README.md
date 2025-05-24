# Proyecto Final: Sistema de Backup Seguro con Algoritmos de Compresión Clásicos, Dask y OpenMP

## Integrantes

| Nombre                    | Correo                      |
|---------------------------|-----------------------------|
| Jose Alejandro Tordecilla | jatordeciz@eafit.edu.co     |
| Juan Andrés Montoya       | jamontoya2@eafit.edu.co     |
| Valeria Corrales          | vcorrales1@eafit.edu.co     |

## Enunciado

Desarrollar un sistema de respaldo seguro que permita a los usuarios seleccionar múltiples carpetas de un disco duro, respaldar todos los archivos de dichas carpetas (incluyendo subcarpetas), comprimirlos en un único archivo de backup utilizando algoritmos de compresión clásicos (ZIP, GZIP o BZIP2) y, opcionalmente, encriptarlo. El archivo de backup generado podrá almacenarse en un disco duro externo, en un servicio de almacenamiento en la nube o dividirse en fragmentos para guardarse en dispositivos USB. El sistema debe implementar técnicas de paralelismo utilizando Dask (si se desarrolla en Python) u OpenMP (si se desarrolla en C++), para optimizar el rendimiento en la compresión, encriptación (si aplica) y transferencia de datos. Se permite el uso de bibliotecas externas para compresión, encriptación y otras funcionalidades.


## Tecnologías:

- C++: Optamos por el uso de C++ debido a que es un lenguaje compilado, robusto y más cercano a la máquina, lo que nos da un control más exhaustivo de los recursos

- Minizip: Esta librería nos provee una implementación ligera de compresión .zip y un control nativo sobre la estructura de las carpetas, ideal para nuestros requerimientos

- Dropbox: Probamos con otras APIs, pero solo dropbox nos permitió realizar la conexión a sus servicios sin errores


## Librerías y Dependencias

El proyecto utiliza las siguientes bibliotecas para implementar sus funcionalidades:

- **[libzip](https://libzip.org/)**: Manipulación de archivos ZIP (compresión/descompresión)
- **[OpenSSL](https://www.openssl.org/)**: Funciones criptográficas para encriptación/desencriptación
- **[OpenMP](https://www.openmp.org/)**: Framework para programación paralela
- **[libcurl](https://curl.se/libcurl/)**: Cliente para transferencia de datos
- **[jsoncpp](https://github.com/open-source-parsers/jsoncpp)**: Manejo de datos en formato JSON

### Instalación de dependencias

En sistemas basados en Debian/Ubuntu, puedes instalar todas las dependencias necesarias con:

```sh
sudo apt-get install libzip-dev libssl-dev libcurl4-openssl-dev libjsoncpp-dev zlib1g-dev
```

## Funcionalidades

### Compresión y Respaldo

- **[Compresión de archivos por partes](./compress.cpp):** El sistema permite dividir el archivo comprimido en múltiples partes de tamaño configurable, facilitando el almacenamiento en dispositivos con espacio limitado o la transferencia por red. Esta funcionalidad trabaja con o sin paralelismo, según la configuración del usuario.

- **Fragmentación inteligente:** Para archivos grandes que exceden el límite de tamaño de fragmento, el sistema los divide automáticamente y mantiene la información necesaria para reconstruirlos durante la descompresión.

- **[Archivo `.ignore`](./main.cpp):** Similar a `.gitignore`, permite especificar patrones para excluir archivos o carpetas durante la compresión:
    - `<nombre_archivo>`: Ignora un archivo específico
    - `/nombre_carpeta`: Ignora todo el contenido de una carpeta
    - `*.extensión`: Ignora todos los archivos con cierta extensión

### Metadatos y Reconstrucción

- **[Archivos de información](./compress.cpp):** Cada parte comprimida incluye un archivo `.info` (no encriptado) que contiene los metadatos necesarios para la reconstrucción. Estructura:
  ```
  # Total partes
  # Parte X de Y
  # Formato: ruta_en_zip | ruta_original
  archivo1.txt | /ruta/completa/al/archivo1.txt
  carpeta/archivo2.jpg | /ruta/completa/al/carpeta/archivo2.jpg
  ```

### Descompresión y Seguridad

- **[Funcionalidad de descompresión](./decompress.cpp):** Sistema completo para restaurar archivos respaldados, capaz de reconstruir archivos fragmentados a partir de múltiples partes ZIP.

- **[Encriptación](./crypto.h):** Opción de cifrado AES para proteger la información utilizando una contraseña definida por el usuario.

### Almacenamiento en la Nube

- **[Conexión con Dropbox](./dropbox_uploader.cpp):** Integración opcional para subir automáticamente los archivos generados a Dropbox mediante tokens de acceso OAuth.

### Rendimiento y Análisis

- **Benchmarks:** Herramientas integradas para comparar el rendimiento de compresión entre el modo serial y paralelo, ayudando a optimizar el uso según las características del sistema.
...


## Paralelismo
Para mejorar el rendimiento del sistema de backup, se ha implementado paralelización utilizando OpenMP tanto en la compresión como en la descompresión. A continuación se muestran los puntos clave donde se aplica esta técnica:

### Paralelismo en la [Compresión](./compress.cpp)
```c++
// Fragmentación y procesamiento paralelo de archivos grandes
#pragma omp parallel for schedule(dynamic)
for (int i = 0; i < fragmentsNeeded; i++) {
    // Cada hilo procesa un fragmento independiente del archivo
    FILE *file = fopen(filePath.string().c_str(), "rb");
    // Lectura del fragmento
    fread(tasks[i].buffer.data(), 1, tasks[i].bytesToRead, file);
    
    // Los fragmentos son procesados en paralelo, aprovechando múltiples cores
}
```

La compresión utiliza paralelismo para:

- Filtrar archivos que deben ser ignorados
- Procesar archivos grandes dividiéndolos en fragmentos
- Comprimir y encriptar múltiples fragmentos simultáneamente


### Paralelismo en la [Descompresión](./decompress.cpp)
```c++
// Procesamiento paralelo de archivos ZIP
#pragma omp parallel for
for (size_t i = 0; i < zipFiles.size(); i++) {
    const auto &zipFile = zipFiles[i];
    // Cada hilo abre y procesa un archivo ZIP independiente
    zip_t *archive = zip_open(zipFile.string().c_str(), 0, &err);
    
    // Recopilación de información de fragmentos con protección de mutex
    #pragma omp critical(fragments)
    {
        for (const auto &fragmentInfo : info.fragments) {
            allFragments[baseName].push_back(fragmentInfo);
        }
    }
}
```

La descompresión paraleliza:

La apertura y análisis inicial de múltiples archivos ZIP
La extracción de archivos normales
La reconstrucción de archivos fragmenta


## Comandos

### Compilación y Herramientas de Desarrollo

**Comandos Make:**

```sh
# Compilar todos los ejecutables
make all

# Compilar solo el compresor
make main

# Compilar solo el descompresor
make descompresor

# Limpiar archivos compilados
make clean

# Formatear código fuente (requiere clang-format)
make format

# Ver ayuda sobre comandos disponibles
make help
```

### Ejecución del Compresor

**Uso:**
```sh
./main -d [carpeta] -o [archivo_zip] -s [tamaño] -e [contraseña_encriptacion] [-p] [-b] [-u]
```

**Opciones:**
- `-d` : Directorio a comprimir (default: `./test`)
- `-o` : Archivo ZIP de salida (default: `./output/archivo_comprimido.zip`)
- `-s` : Tamaño máximo en MB por fragmento (default: `50`)
- `-e` : Contraseña para la encriptación (opcional)
- `-p` : Usar procesamiento paralelo (default: desactivado)
- `-b` : Ejecutar benchmark comparativo entre modo serial y paralelo
- `-u` : Subir archivos ZIP generados a Dropbox (requiere configuración previa)
- `-h` : Mostrar ayuda

### Ejecución del Descompresor

**Uso:**
```sh
./descompresor -i [carpeta_del_zip] -o [carpeta_output] -p [contraseña_encriptación]
```

**Opciones:**
- `-i` : Carpeta que contiene los archivos ZIP (default: `./output`)
- `-o` : Carpeta destino para los archivos descomprimidos (default: `./extracted`)
- `-p` : Contraseña para la desencriptación (solo necesaria si los archivos fueron encriptados)