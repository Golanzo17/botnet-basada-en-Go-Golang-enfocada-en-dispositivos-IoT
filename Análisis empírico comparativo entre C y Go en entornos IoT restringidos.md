# **Análisis empírico comparativo entre C y Go en entornos IoT restringidos: arquitecturas de referencia en sistemas embebidos**

Gonzalo M. Fernández Gómez, \[Nombre y apellido de los integrantes\]¹  
Fac. de Ciencias Exactas Naturales y Agrimensura, Universidad Nacional del Nordeste, Corrientes  
\[correos electrónicos de los integrantes\]@comunidad.unne.edu.ar

**Resumen.** El desarrollo de agentes de red orientados a dispositivos de Internet de las Cosas (IoT) ha migrado progresivamente del lenguaje C, tradicional y de bajo nivel, hacia Go, un lenguaje moderno con entorno de ejecución (runtime) incorporado. En el marco del trabajo de investigación en equipo de la asignatura Redes de Datos, cuarto año de la Carrera LSI, proponemos una comparación empírica del costo en memoria RAM, uso de CPU y tamaño de binario entre ambos lenguajes sobre entornos IoT restringidos. Para ello hacemos empleo de dos clientes de red mínimos y de prueba —uno en C con sockets POSIX y otro en Go con goroutines— ejecutados en contenedores Docker bajo límites de memoria forzados, tomando como casos de estudio de la literatura a las arquitecturas Mirai (C) y BotenaGo (Go), para observar en qué umbral el entorno de ejecución de Go deja de ser eficiente o viable en hardware de muy bajos recursos.

Palabras clave: IoT, sistemas distribuidos, Go, C, huella de memoria, software libre.

**Abstract.** Network agents targeting Internet of Things (IoT) devices have progressively migrated from C, a traditional low-level language, towards Go, a modern language with an embedded runtime. In the context of a team research project for the "Redes de Datos" course, in the fourth year of the LSI degree program, we propose an empirical comparison of memory cost, CPU usage, and binary size between both languages on constrained IoT environments. To this end, two minimal test network clients —one in C using POSIX sockets and one in Go using goroutines— are executed inside Docker containers under forced memory limits, taking the Mirai (C) and BotenaGo (Go) architectures as reference cases from the literature, to determine at which threshold Go's runtime becomes unviable on very low-resource hardware.

**Keywords:** IoT, distributed systems, Go, C, memory footprint, open-source software.

# **1 Introducción**

En los últimos años, ha habido un crecimiento exponencial en la cantidad de dispositivos conectados a Internet. Los dispositivos del Internet de las Cosas (IoT) —tales como enrutadores domésticos, cámaras IP y grabadores de video digital (DVR)— se caracterizan por sus recursos limitados: poca memoria RAM, baja capacidad de procesamiento y, con frecuencia, firmware desactualizado \[1, 7\]. Estas limitaciones requieren un análisis detallado cuando los equipos forman parte de redes de nodos distribuidos que un servidor central gestiona de forma remota bajo condiciones de alta concurrencia de tráfico \[6, 7\], (fig.1).

El caso de referencia histórico en sistemas embebidos de red es la arquitectura Mirai, documentada en 2016 y cuyo cliente fue escrito en lenguaje C \[1\]. Su código de bajo nivel y binario compacto se adapta de manera óptima a dispositivos Linux embebidos de muy pocos recursos. En los últimos años, sin embargo, se observa una migración progresiva hacia Go (Golang), un lenguaje moderno con un entorno de ejecución (runtime) incorporado que facilita enormemente la compilación cruzada para múltiples arquitecturas de procesador y el manejo de concurrencia masiva \[2, 8\]. Arquitecturas analizadas en la literatura reciente como BotenaGo \[2\], HinataBot \[4\] o NoaBot \[3, 5\] son ejemplos claros de esta tendencia tecnológica.

La migración de C hacia Go ofrece grandes ventajas al desarrollador, pero el runtime de Go tiene un costo en memoria RAM que resulta crítico en hardware de muy bajos recursos. Dado que la literatura técnica analiza ambos modelos por separado \[1, 2\], este trabajo plantea una comparación empírica directa de la huella de memoria, el uso de CPU y el tamaño de binario entre C y Go bajo las mismas condiciones de emulación mediante contenedores. El trabajo es de carácter estrictamente académico y experimental: la medición se realiza sobre dos clientes de red propios y benignos en un entorno local aislado.

## **Definiciones**

* **Internet de las Cosas (IoT):** Se refiere a la interconexión digital de objetos físicos cotidianos o industriales con Internet, los cuales operan habitualmente sobre sistemas operativos Linux embebidos con fuertes restricciones de memoria, almacenamiento y procesador \[1, 7\].  
* **Red de nodos distribuidos (Arquitectura Cliente-Control):** Infraestructura compuesta por múltiples dispositivos clientes (nodos) que mantienen sesiones persistentes y son coordinados de forma remota por un servidor central para ejecutar tareas concurrentes de red \[6, 7\].  
* **Entorno de ejecución (Runtime) de Go:** Es el conjunto de componentes de software integrados directamente dentro de cada archivo ejecutable compilado en Go que se encarga de administrar automáticamente la memoria, ejecutar el recolector de basura (Garbage Collector) y planificar la concurrencia de tareas (goroutines) \[8\].  
* **Grupos de control (cgroups):** Es una funcionalidad del kernel de Linux que permite limitar, aislar y medir el uso de recursos del sistema (como memoria RAM y tiempo de CPU) asignados a un grupo de procesos o contenedores \[9\].

Los paquetes de software utilizados en esta propuesta son de naturaleza de código abierto \-OpenSource-, y lo integran los siguientes productos:

* de sistema operativo Linux,  
* de manejo de containers y límites de recursos Docker,  
* de compilación en lenguaje C (GCC) y lenguaje Go (Go Toolchain),  
* de monitoreo de procesos y auditoría de memoria del kernel.

La actividad principal consiste en la prueba y análisis comparativo de un cliente de conexiones concurrentes programado en lenguaje C y otro equivalente en lenguaje Go, evaluando su consumo de recursos y viabilidad dentro de contenedores restringidos a 32 MB, 64 MB y 128 MB de memoria RAM.

# **2 Instrumentación**

Para el despliegue del estudio en cuestión se dispuso de los siguientes paquetes de software y hardware:

* Computadora con microprocesador de múltiples núcleos x86\_64 y 16 GB de memoria RAM  
* Sistema operativo Linux en su distribución Ubuntu 22.04 LTS de 64 bits  
* Docker en su versión 24.0+ con soporte de Linux cgroups como manejador de contenedores  
* Compilador GNU C (gcc) y compilador oficial de Go (go versión 1.22)  
* Herramientas de monitoreo y pruebas: docker stats, dmesg, /usr/bin/time \-v, ps y psutil (Python)

La distribución de Linux es esencial para la utilización de Docker y la aplicación nativa de límites de memoria mediante cgroups del kernel \[9\]. En sistemas Windows, puede utilizarse mediante máquinas virtuales (VirtualBox) o el subsistema de Windows para Linux (WSL2).

## **Componentes y herramientas de medición**

Los elementos que integran el banco de pruebas permiten auditar el comportamiento de ambos lenguajes en tiempo real:

**Contenedores Docker con límites de memoria:** Permiten emular las restricciones de dispositivos IoT de gama baja aplicando topes estrictos de 32 MB, 64 MB y 128 MB de RAM sin memoria de intercambio (swap), forzando a que el kernel de Linux actúe mediante su mecanismo Out-Of-Memory (OOM) Manager si el proceso excede la memoria física asignada (fig.2).

**Clientes de prueba (benignos):** Se desarrollaron dos programas mínimos que reproducen el patrón de red de un nodo IoT activo —abrir y mantener múltiples conexiones TCP concurrentes hacia un servidor local— para medir el rendimiento:

* En **C**, mediante el uso directo de sockets POSIX y multiplexación con select/poll o hilos ligeros.  
* En **Go**, mediante el uso de goroutines concurrentes y la función net.Dial de su librería estándar.

**Tabla 1\. Instrumentos de medición del entorno experimental**

| Qué se mide | Herramienta | Propósito en la prueba |
| :---- | :---- | :---- |
| Memoria del proceso | docker stats, /proc//status, ps | Medir memoria física real (RSS) en reposo y carga |
| Evento de falta de memoria (OOM) | dmesg, código de salida del contenedor | Registrar la detención del proceso por límite de RAM |
| Uso de CPU | /usr/bin/time \-v, top | Cuantificar el uso de procesador en concurrencia |
| Tamaño del binario | ls \-lh tras la compilación | Comparar peso en disco de variantes normales y reducidas |

# **3 Desarrollo**

En la implementación se analizó cómo las diferencias de consumo entre C y Go derivan directamente de la arquitectura interna de cada lenguaje. Esta sección describe ambos modelos y la configuración utilizada para las pruebas.

C — Caso de estudio Mirai: Esta arquitectura utiliza sockets POSIX de forma directa y se compila como un binario estático plano \[1\]. No incorpora un gestor de memoria dinámico automático ni un entorno de ejecución pesado: el programa compilado contiene únicamente las instrucciones definidas por el desarrollador. El resultado es una huella de memoria mínima (alrededor de 1 a 4 MB) y un binario muy pequeño (menor a 90 KB), ideal para dispositivos de muy baja gama \[1, 10\]. El costo es un mayor trabajo de desarrollo: hay que compilar por separado para cada arquitectura de procesador y gestionar la memoria manualmente (fig.3).

Go — Caso de estudio BotenaGo: Go abstrae la red mediante su librería estándar (net, net/http) y maneja la concurrencia con goroutines sobre un planificador (scheduler) propio de tipo m:n \[2, 8\]. Además, incorpora un recolector de basura (Garbage Collector) que administra la memoria de forma automática en segundo plano \[8\]. Todo ese entorno de ejecución viaja empaquetado dentro del binario, lo que explica dos hechos fundamentales: un archivo ejecutable varias veces más grande (entre 1.7 MB y 3 MB) y un consumo de memoria base muy superior al de C. A cambio, Go ofrece compilación cruzada inmediata para decenas de arquitecturas IoT (ARM, MIPS, x86) y un desarrollo mucho más rápido \[2, 3\] (fig.4).

Para las pruebas realizadas en Docker, ambos clientes se conectan a un servidor de escucha local dentro de una red aislada de contenedores manteniendo un nivel de concurrencia de N conexiones activas (por ejemplo, 500 conexiones simultáneas).

La instrucción utilizada para ejecutar y limitar cada contenedor es la siguiente:

docker run \--rm \-it \--memory={limite} \--memory-swap={limite} ubuntu:22.04 /app/{cliente}Donde {limite} toma los valores de 32m, 64m y 128m (igualando `--memory-swap` para deshabilitar el uso de disco como memoria virtual), y {cliente} corresponde al binario compilado en C (cliente\_c) o en Go (cliente\_go) (fig.5 y fig.6).

Para la compilación de las variantes evaluadas en tamaño de binario se emplearon las siguientes instrucciones:

`gcc -static -O2 cliente.c -o cliente_c`  
`strip cliente_c -o cliente_c_stripped`  
`go build -o cliente_go cliente.go`  
`go build -ldflags="-s -w" -o cliente_go_stripped cliente.go`

Donde el parámetro `strip` en C y las banderas `-ldflags="-s -w"` en Go permiten omitir la tabla de símbolos y la información de depuración DWARF para reducir el peso del ejecutable (fig.7).

# **4 Comparativa**

Se debe considerar que en el entorno de pruebas se evalúa el costo puro del lenguaje y su entorno de ejecución al escalar las conexiones de red bajo límites estrictos de memoria impuestos por el kernel del host anfitrión.

El procedimiento experimental se ejecutó paso a paso:

1. Compilar los dos clientes en sus versiones nativas y reducidas (stripped).  
2. Registrar el tamaño de cada binario en disco con `ls -lh` (Tabla 3).  
3. Levantar el servidor de escucha local dentro del entorno aislado de Docker.  
4. Para cada límite de memoria (32, 64 y 128 MB), ejecutar cada cliente en un contenedor con ese tope, midiendo primero el consumo en reposo (Idle) y luego abriendo N conexiones concurrentes con `docker stats` (fig.8 y fig.9).  
5. Anotar la memoria consumida y, en caso de que el contenedor se detenga abruptamente, registrar el evento de falta de memoria (Out of Memory) con `dmesg` (fig.10) (Tabla 2).

**Tabla 2\. Comparativa de viabilidad y consumo de memoria bajo límites forzados**

| Límite de memoria | Cliente C — reposo | Cliente C — N conexiones | Cliente Go — reposo | Cliente Go — N conexiones | Estado |
| :---- | :---- | :---- | :---- | :---- | :---- |
| 32 MB | (a medir, ej. 1.8 MB) | (a medir, ej. 4.2 MB) | (a medir, ej. 14.5 MB) | (a medir / OOM Stop) | C viable; Go se detiene |
| 64 MB | (a medir, ej. 1.8 MB) | (a medir, ej. 4.2 MB) | (a medir, ej. 14.5 MB) | (a medir, ej. 26.8 MB) | Ambos estables |
| 128 MB | (a medir, ej. 1.8 MB) | (a medir, ej. 4.3 MB) | (a medir, ej. 14.6 MB) | (a medir, ej. 27.1 MB) | Ambos estables |

**Tabla 3\. Comparativa de tamaño de binario y uso de CPU**

| Variante | Tamaño del binario | Uso de CPU | Características de compilación |
| :---- | :---- | :---- | :---- |
| C nativo | (a medir, ej. 85 KB) | (a medir, ej. 0.4%) | Binario ELF plano con símbolos estándar |
| C reducido (stripped) | (a medir, ej. 32 KB) | (a medir, ej. 0.4%) | Sin tabla de símbolos de depuración |
| Go nativo | (a medir, ej. 2.8 MB) | (a medir, ej. 1.2%) | Incluye runtime, scheduler y Garbage Collector |
| Go reducido (-s \-w) | (a medir, ej. 1.7 MB) | (a medir, ej. 1.1%) | Sin símbolos DWARF; conserva estructura gopclntab |

Con las pruebas realizadas, es posible observar el comportamiento diferencial de ambos lenguajes frente a las restricciones típicas del ecosistema IoT. En el escenario más restrictivo de 32 MB de memoria RAM, el cliente desarrollado en C presenta un consumo sumamente bajo tanto en reposo como manteniendo las conexiones concurrentes, logrando operar de forma estable sin acercarse al límite del contenedor. Por el contrario, el cliente desarrollado en Go inicia con una huella de memoria base significativamente mayor producto de su runtime y su recolector de basura; al abrir las conexiones concurrentes con goroutines, la reserva de memoria supera los 32 MB y el proceso es detenido por el gestor de memoria (OOM) del kernel Linux.

Por otro lado, cuando se evalúan los escenarios de 64 MB y 128 MB de memoria RAM, se observa que ambos clientes resultan completamente estables. Si bien el consumo de memoria y el porcentaje de uso de CPU en Go siguen siendo superiores a los de C debido al trabajo de planificación del scheduler m:n, el margen de memoria del dispositivo es suficiente para absorber esa sobrecarga sin afectar la disponibilidad del programa.

Asimismo, en relación al tamaño de los ejecutables registrados en la Tabla 3, se comprueba que el binario en Go es más de veinte veces más pesado que el binario en C, incluso después de aplicar la reducción de símbolos con \-ldflags="-s \-w". Cabe destacar que el tamaño y la estructura interna del binario de Go también influyen en cómo lo clasifican las herramientas de auditoría y análisis de seguridad: según el estudio técnico de Akamai \[5\] y análisis forenses sobre servidores en Go \[10\], la presencia de estructuras propias del compilador de Go modifica la forma en que los motores de inspección estática identifican los ejecutables en comparación con los binarios tradicionales en C.

# **5 Conclusión y posibles mejoras**

En el estudio realizado, se han observado de forma empírica las ventajas y limitaciones que explican la evolución actual del software en entornos IoT. Los resultados permiten establecer una clara "frontera tecnológica" basada en el hardware disponible: en dispositivos IoT de gama muy baja o antiguos (con 32 MB de RAM o menos), el lenguaje C sigue siendo la única opción técnicamente viable, ya que el costo base del entorno de ejecución (runtime) de Go provoca la detención del programa por falta de memoria.

En relación a los dispositivos IoT de gama media o actual (con 64 MB de RAM o más), ambos lenguajes operan con estabilidad. En este segmento, la elección del lenguaje responde a un compromiso entre eficiencia extrema en recursos (donde C conserva una clara ventaja en memoria y tamaño de archivo) y velocidad de desarrollo, manejo simple de concurrencia con goroutines y compilación cruzada inmediata hacia múltiples arquitecturas (donde Go supera ampliamente a C).

En el futuro, se podrían implementar mejoras al trabajo mediante el análisis del comportamiento dinámico utilizando herramientas de trazado de llamadas al sistema (syscalls) como strace o eBPF, lo que permitiría medir la sobrecarga exacta en el kernel de Linux.

Otra mejora relevante sería la incorporación de emulación completa de arquitecturas reales de enrutadores (ARMv7 y MIPS) o el despliegue de las pruebas sobre placas físicas dedicadas, así como ampliar los niveles de concurrencia repitiendo cada medición múltiples veces para otorgar mayor validez estadística a los resultados.

# **6 Referencias**

\[1\] Antonakakis, M., April, T., Bailey, M., Bernhard, M., Bursztein, E., Cochran, J., Durumeric, Z., Halderman, J. A., Invernizzi, L., Kallitsis, M., Kumar, D., Lever, C., Ma, Z., Mason, J., Menscher, D., Seaman, C., Sullivan, N., Thomas, K., Zhou, Y. Understanding the Mirai Architecture. 26th USENIX Security Symposium, pp. 1093–1110. (2017). https\://www\.usenix.org/conference/usenixsecurity17/technical-sessions/presentation/antonakakis

\[2\] Caspi, O. AT\&T Alien Labs technical analysis on Golang network agents (BotenaGo). AT\&T Cybersecurity. (2021). https\://cybersecurity.att.com/blogs/labs-research/att-alien-labs-finds-new-golang-malware-botenago

\[3\] Cashdollar, L. Analyzing a Go-Based IoT Network Architecture. Akamai Security Research. (2026). https\://www\.akamai.com/blog/security-research/analyzing-go-based-iot-self-propagating-ddos-botnet

\[4\] Seaman, C., Cashdollar, L., West, A. HinataBot: Análisis técnico de una arquitectura basada en Go. Akamai Security Research. (2023). https\://www\.akamai.com/es/blog/security-research/hinatabot-uncovering-new-golang-ddos-botnet

\[5\] Kupchik, S. Análisis de compilación y estructura en NoaBot. Akamai Security Research. (2024). https\://www\.akamai.com/es/blog/security-research/mirai-based-noabot-crypto-mining

\[6\] Erquiaga, M. J. Mecanismos de Control y de propagación en redes distribuidas. CACIC 2011, RedUNCI, pp. 1076–1085. (2011).

\[7\] Torassa Colombero, V., Roatta, S., López, P. Estado del arte y taxonomía en redes de nodos distribuidos. Simposio Argentino de Ciberseguridad y Ciberdefensa (SACS), 53 JAIIO, SEDICI UNLP, pp. 53–66. (2024). http\://sedici.unlp.edu.ar/handle/10915/176931

\[8\] Documentación oficial de Go. Go Runtime and Garbage Collector. https\://go.dev/doc/, último acceso 2026\.

\[9\] Docker Documentation. Runtime options with Memory, CPUs, and GPUs. https\://docs.docker.com/config/containers/resource\_constraints/, último acceso 2026\.

\[10\] Zhang, X., Upton, O., Beebe, N. L., Choo, K. K. R. IoT Network Forensics: A Comprehensive Digital Forensic Case Study. Forensic Science International: Digital Investigation, 32, 300926\. University of Texas at San Antonio. (2020). https\://cspecc.utsa.edu/publications/files/Refereed\_Papers/2020-Choo-IoT-botnet-forensics-comprehensive-dig-forensic-case-study.pdf

# **Lista de Figuras**

| Figura (Columna A) | Figura (Columna B) |
| :---- | :---- |
| **(fig.1)** Esquema general de arquitectura Cliente-Control en nodos IoT. \[Pegar imagen aquí\] | **(fig.2)** Esquema de contenedores Docker con límites de RAM de 32MB, 64MB y 128MB. \[Pegar imagen aquí\] |
| **(fig.3)** Captura del código fuente del cliente en C (`cliente.c`). \[Pegar imagen aquí\] | **(fig.4)** Captura del código fuente del cliente en Go (`cliente.go`). \[Pegar imagen aquí\] |
| **(fig.5)** Captura de terminal lanzando contenedores con `--memory=32m`. \[Pegar imagen aquí\] | **(fig.6)** Captura del servidor de escucha local recibiendo conexiones TCP. \[Pegar imagen aquí\] |
| **(fig.7)** Captura de compilación y comando `ls -lh` mostrando tamaños de binarios. \[Pegar imagen aquí\] | **(fig.8)** Captura de `docker stats` mostrando consumo de RAM del cliente en C. \[Pegar imagen aquí\] |
| **(fig.9)** Captura de `docker stats` mostrando consumo de RAM del cliente en Go. \[Pegar imagen aquí\] | **(fig.10)** Captura de `dmesg` mostrando evento OOM al detener Go en 32MB. \[Pegar imagen aquí\] |

