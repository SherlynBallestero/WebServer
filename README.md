# WebServer
# Integrantes
Sherlyn Ballestero Cruz.

Maria de Lourdes Choy.

Javier Rodriguez

# Informe:
El proyecto empieza en la linea 27 del archivo webserver.c con la funcion main. En la línea 47 llamamos a la funcion Open_listenfd, la cual crea un socket, lo enlaza a la dirección del puerto y lo devuelve(línea 896 de wslibs.c ). Luego acepta la conexión y la procesa dentro de un hijo, para permitir la solicitud de multiples clientes. Este proceso se lleva a cabo en la función doit(línea 72), el cual empieza parseando la solicitud del cliente y obteniendo el método, y la dirección uri. Una vez con esta ultima, realiza la acción correspondiente, ya sea descargar o navegar, haciendo una diferenciación de casos en este ultimo, devolviendo una respuesta estática(línea 159) o dinámica(línea 169). Esta diferenciación se realiza en la función parse_uri(línea 194), y en caso de ser estática, genera una pagina web y la almacena en el servidor usando la función open_read_dir(línea 339). Dentro de esta ultima función se pueden ordenar los resultados de acuerdo con los diferentes criterios (nombre, tamaño, fecha) (linea539). Si la respuesta estática solo lee la pagina web almacenada en el servidor y la entrega(línea 245), si es dinámica corre el programa CGI especificado en la entrada y devuelve su salida(línea 289).

# 1) Para compilar el proyecto por favor hacer en la consola:

gcc -o webserver webserver.c wslibs.c

# 2) Para ejecutar el webserver haga en la consola con el puerto y el directorio raiz deseado:

./webserver \<puerto\> \<directorio\>
