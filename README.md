# Proyecto Final: Sistema de Backup Seguro con Algoritmos de Compresión Clásicos, Dask y OpenMP

## Integrantes

| Nombre                    | Correo                      |
|---------------------------|-----------------------------|
| Jose Alejandro Tordecilla | jatordeciz@eafit.edu.co     |
| Juan Andrés Montoya       | -                           |
| Valeria Corrales          | -                           |

## Enunciado

Desarrollar un sistema de respaldo seguro que permita a los usuarios seleccionar múltiples carpetas de un disco duro, respaldar todos los archivos de dichas carpetas (incluyendo subcarpetas), comprimirlos en un único archivo de backup utilizando algoritmos de compresión clásicos (ZIP, GZIP o BZIP2) y, opcionalmente, encriptarlo. El archivo de backup generado podrá almacenarse en un disco duro externo, en un servicio de almacenamiento en la nube o dividirse en fragmentos para guardarse en dispositivos USB. El sistema debe implementar técnicas de paralelismo utilizando Dask (si se desarrolla en Python) u OpenMP (si se desarrolla en C++), para optimizar el rendimiento en la compresión, encriptación (si aplica) y transferencia de datos. Se permite el uso de bibliotecas externas para compresión, encriptación y otras funcionalidades.

## Librerías

**Minizip:** Proporciona soporte nativo para carpetas y archivos.

```sh
sudo apt-get install libminizip-dev zlib1g-dev
```

Compilación:

```sh
make
```
--
## Funcionalidades

- **Compresión de archivos por partes:** Permite dividir el archivo comprimido en varias partes.
- **Archivo `.ignore`:** Similar a `.gitignore`, permite especificar archivos o carpetas a excluir durante la compresión mediante los siguientes patrones:
    - `<nombre_archivo>`: Ignora un archivo específico.
    - `/nombre_carpeta`: Ignora todo el contenido de una carpeta.
    - `*.extensión`: Ignora todos los archivos con cierta extensión.

## Comandos

**Uso:**  
```sh
./main -d [carpeta] -o [archivo_zip] -p [número_de_partes]
```

**Opciones:**
- `-d` : Directorio a comprimir (default: `./test`)
- `-o` : Archivo ZIP de salida (default: `./output/archivo_comprimido.zip`)
- `-p` : Número de partes en las que dividir la compresión (opcional).  
    Si se omite, se creará un único archivo ZIP.