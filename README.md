# Proyecto Final: Sistema de Backup Seguro con Algoritmos de Compresión Clásicos, Dask y OpenMP

## Integrantes
|Nombre|Correo|
|-------|------|
|Jose Alejandro Tordecilla| jatordeciz@eafit.edu.co|
|Juan Andrés Montoya| -|
|Valeria Corrales| -|


## Enunciado
Desarrollar un sistema de respaldo seguro que permita a los usuarios seleccionar múltiples carpetas de un disco duro, respaldar todos los archivos de dichas carpetas (incluyendo sus subcarpetas), comprimirlos en un único archivo de backup utilizando algoritmos de compresión clásicos (ZIP, GZIP, o BZIP2), y opcionalmente encriptarlo. El archivo de backup generado podrá almacenarse en un disco duro externo, en un servicio de almacenamiento en la nube, o dividirse en fragmentos para guardarse en dispositivos USB. El sistema debe implementar técnicas de paralelismo utilizando Dask si se desarrolla en Python o OpenMP si se desarrolla en C++, para optimizar el rendimiento en la compresión, encriptación (si aplica) y transferencia de datos. Se permite el uso de bibliotecas externas para compresión, encriptación y otras funcionalidades.
