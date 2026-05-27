#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <fstream>
#include <ctime>
#include <sstream>
#include <unordered_map>
#include <iomanip>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <openssl/sha.h>

using namespace std;

struct Cliente {
    int socket;
    string nombre;
    string ip;
    bool autenticado;
};

struct Usuario {
    string hashContrasena;
    string rol;
};

vector<Cliente> clientes;
mutex mtx;
unordered_map<string, Usuario> credenciales;

string horaActual() {
    time_t ahora = time(nullptr);
    tm* t = localtime(&ahora);

    char buffer[10];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", t);

    return string(buffer);
}

string fechaHoraActual() {
    time_t ahora = time(nullptr);
    tm* t = localtime(&ahora);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", t);

    return string(buffer);
}

string trim(const string& s) {
    size_t inicio = s.find_first_not_of(" \t\r\n");

    if (inicio == string::npos) {
        return "";
    }

    size_t fin = s.find_last_not_of(" \t\r\n");

    return s.substr(inicio, fin - inicio + 1);
}

string hashSHA256(const string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256(
        reinterpret_cast<const unsigned char*>(input.c_str()),
        input.size(),
        hash
    );

    stringstream ss;

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

void guardarEvento(const string& tipo, const string& detalle) {
    ofstream archivo("chat.log", ios::app);

    if (archivo.is_open()) {
        archivo << "[" << fechaHoraActual() << "] [" << tipo << "] " << detalle << endl;
    }
}

void enviarMensaje(int socketCliente, const string& mensaje) {
    send(socketCliente, mensaje.c_str(), mensaje.size(), 0);
}

void cargarUsuarios() {
    credenciales.clear();

    ifstream archivo("usuarios.txt");
    string linea;

    if (!archivo.is_open()) {
        guardarEvento("ERROR", "No se pudo abrir usuarios.txt");
        return;
    }

    while (getline(archivo, linea)) {
        stringstream ss(linea);

        string usuario;
        string hashContrasena;
        string rol;

        getline(ss, usuario, ':');
        getline(ss, hashContrasena, ':');
        getline(ss, rol, ':');

        usuario = trim(usuario);
        hashContrasena = trim(hashContrasena);
        rol = trim(rol);

        if (!usuario.empty() && !hashContrasena.empty() && !rol.empty()) {
            credenciales[usuario] = {hashContrasena, rol};
        }
    }

    guardarEvento("SERVER", "Usuarios cargados desde usuarios.txt total=" + to_string(credenciales.size()));
}

bool guardarNuevoUsuario(const string& usuario, const string& hashContrasena, const string& rol) {
    ofstream archivo("usuarios.txt", ios::app);

    if (!archivo.is_open()) {
        guardarEvento("ERROR", "No se pudo abrir usuarios.txt para registrar usuario=" + usuario);
        return false;
    }

    archivo << usuario << ":" << hashContrasena << ":" << rol << endl;

    return true;
}

bool autenticarUsuario(const string& usuario, const string& contrasena) {
    if (!credenciales.count(usuario)) {
        return false;
    }

    string hashIngresado = hashSHA256(contrasena);

    return hashIngresado == credenciales[usuario].hashContrasena;
}

string obtenerRolUsuario(const string& usuario) {
    if (credenciales.count(usuario)) {
        return credenciales[usuario].rol;
    }

    return "desconocido";
}

bool esAdmin(const string& usuario) {
    return obtenerRolUsuario(usuario) == "admin";
}

bool usuarioConectadoSinLock(const string& nombre) {
    for (const auto& cliente : clientes) {
        if (cliente.autenticado && cliente.nombre == nombre) {
            return true;
        }
    }

    return false;
}

bool usuarioConectado(const string& nombre) {
    lock_guard<mutex> lock(mtx);
    return usuarioConectadoSinLock(nombre);
}

string obtenerNombreCliente(int socketCliente) {
    lock_guard<mutex> lock(mtx);

    for (const auto& cliente : clientes) {
        if (cliente.socket == socketCliente) {
            return cliente.nombre;
        }
    }

    return "Desconocido";
}

string obtenerIpCliente(int socketCliente) {
    lock_guard<mutex> lock(mtx);

    for (const auto& cliente : clientes) {
        if (cliente.socket == socketCliente) {
            return cliente.ip;
        }
    }

    return "desconocida";
}

void eliminarCliente(int socketCliente) {
    lock_guard<mutex> lock(mtx);

    clientes.erase(
        remove_if(
            clientes.begin(),
            clientes.end(),
            [socketCliente](const Cliente& c) {
                return c.socket == socketCliente;
            }
        ),
        clientes.end()
    );
}

string listaUsuarios() {
    lock_guard<mutex> lock(mtx);

    string resultado = "Usuarios conectados:\n";
    bool hayUsuarios = false;

    for (const auto& cliente : clientes) {
        if (cliente.autenticado) {
            string rol = obtenerRolUsuario(cliente.nombre);
            resultado += "- " + cliente.nombre + " (" + rol + ") [" + cliente.ip + "]\n";
            hayUsuarios = true;
        }
    }

    if (!hayUsuarios) {
        resultado += "(ninguno)\n";
    }

    return resultado;
}

string listaUsuariosRaw() {
    lock_guard<mutex> lock(mtx);

    string resultado = "USERS_LIST:";
    bool primero = true;

    for (const auto& cliente : clientes) {
        if (cliente.autenticado) {
            if (!primero) {
                resultado += ",";
            }

            resultado += cliente.nombre;
            primero = false;
        }
    }

    resultado += "\n";

    return resultado;
}

string ayudaComandos() {
    string ayuda;

    ayuda += "\n========== COMANDOS DISPONIBLES ==========\n\n";

    ayuda += "/ayuda\n";
    ayuda += "Muestra esta lista de comandos.\n\n";

    ayuda += "/usuarios\n";
    ayuda += "Muestra los usuarios conectados al servidor.\n\n";

    ayuda += "/msg usuario mensaje\n";
    ayuda += "Envia un mensaje privado a otro usuario.\n";
    ayuda += "Ejemplo: /msg juan Hola, como estas?\n\n";

    ayuda += "/registrar usuario contrasena rol\n";
    ayuda += "Registra un nuevo usuario en el sistema.\n";
    ayuda += "Solo puede usarlo un usuario con rol admin.\n";
    ayuda += "Roles validos: admin, usuario\n";
    ayuda += "Ejemplo: /registrar pedro 1234 usuario\n\n";

    ayuda += "/expulsar usuario\n";
    ayuda += "Expulsa a un usuario conectado.\n";
    ayuda += "Solo puede usarlo un usuario con rol admin.\n";
    ayuda += "Ejemplo: /expulsar pedro\n\n";

    ayuda += "/salir\n";
    ayuda += "Cierra la sesion y desconecta al cliente.\n\n";

    ayuda += "==========================================\n";

    return ayuda;
}

void broadcastUsuariosRaw() {
    string raw = listaUsuariosRaw();

    lock_guard<mutex> lock(mtx);

    for (const auto& cliente : clientes) {
        if (cliente.autenticado) {
            send(cliente.socket, raw.c_str(), raw.size(), 0);
        }
    }
}

void enviarATodos(const string& mensaje, int emisor) {
    lock_guard<mutex> lock(mtx);

    for (const auto& cliente : clientes) {
        if (cliente.autenticado && cliente.socket != emisor) {
            send(cliente.socket, mensaje.c_str(), mensaje.size(), 0);
        }
    }
}

void enviarATodosIncluyendoEmisor(const string& mensaje) {
    lock_guard<mutex> lock(mtx);

    for (const auto& cliente : clientes) {
        if (cliente.autenticado) {
            send(cliente.socket, mensaje.c_str(), mensaje.size(), 0);
        }
    }
}

bool enviarPrivado(const string& remitente, const string& destinatario, const string& texto) {
    lock_guard<mutex> lock(mtx);

    string mensaje = "[" + horaActual() + "] [Privado] " + remitente + ": " + texto + "\n";

    for (const auto& cliente : clientes) {
        if (cliente.autenticado && cliente.nombre == destinatario) {
            send(cliente.socket, mensaje.c_str(), mensaje.size(), 0);
            return true;
        }
    }

    return false;
}

bool registrarUsuario(
    const string& solicitante,
    const string& nuevoUsuario,
    const string& nuevaContrasena,
    const string& nuevoRol,
    string& respuesta
) {
    lock_guard<mutex> lock(mtx);

    if (!esAdmin(solicitante)) {
        respuesta = "No tienes permisos para registrar usuarios.\n";
        guardarEvento("PERMISSION_DENIED", "usuario=" + solicitante + " intento=/registrar");
        return false;
    }

    if (nuevoUsuario.empty() || nuevaContrasena.empty() || nuevoRol.empty()) {
        respuesta = "Uso: /registrar usuario contrasena rol\n";
        respuesta += "Roles validos: admin, usuario\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/registrar motivo=argumentos_incompletos");
        return false;
    }

    if (nuevoRol != "admin" && nuevoRol != "usuario") {
        respuesta = "Rol invalido. Usa: admin o usuario\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/registrar motivo=rol_invalido rol=" + nuevoRol);
        return false;
    }

    if (nuevoUsuario.find(':') != string::npos || nuevoUsuario.find(' ') != string::npos) {
        respuesta = "El usuario contiene caracteres invalidos.\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/registrar motivo=usuario_invalido nuevoUsuario=" + nuevoUsuario);
        return false;
    }

    if (nuevaContrasena.find(':') != string::npos || nuevaContrasena.find(' ') != string::npos) {
        respuesta = "La contrasena contiene caracteres invalidos.\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/registrar motivo=contrasena_invalida nuevoUsuario=" + nuevoUsuario);
        return false;
    }

    if (credenciales.count(nuevoUsuario)) {
        respuesta = "Ese usuario ya existe.\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/registrar motivo=usuario_existente nuevoUsuario=" + nuevoUsuario);
        return false;
    }

    string hashNuevaContrasena = hashSHA256(nuevaContrasena);

    if (!guardarNuevoUsuario(nuevoUsuario, hashNuevaContrasena, nuevoRol)) {
        respuesta = "No se pudo guardar el nuevo usuario en usuarios.txt\n";
        guardarEvento("ERROR", "usuario=" + solicitante + " accion=registrar fallo_escritura nuevoUsuario=" + nuevoUsuario);
        return false;
    }

    credenciales[nuevoUsuario] = {hashNuevaContrasena, nuevoRol};

    respuesta = "Usuario registrado correctamente: " + nuevoUsuario + " con rol " + nuevoRol + "\n";

    guardarEvento("USER_CREATED", "admin=" + solicitante + " nuevoUsuario=" + nuevoUsuario + " rol=" + nuevoRol);

    return true;
}

bool expulsarUsuario(const string& solicitante, const string& objetivo, string& respuesta) {
    if (!esAdmin(solicitante)) {
        respuesta = "No tienes permisos para expulsar usuarios.\n";
        guardarEvento("PERMISSION_DENIED", "usuario=" + solicitante + " intento=/expulsar objetivo=" + objetivo);
        return false;
    }

    if (objetivo.empty()) {
        respuesta = "Uso: /expulsar usuario\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/expulsar motivo=argumentos_incompletos");
        return false;
    }

    if (objetivo == solicitante) {
        respuesta = "No puedes expulsarte a ti mismo.\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/expulsar motivo=autoexpulsion");
        return false;
    }

    int socketObjetivo = -1;
    string ipObjetivo = "desconocida";

    {
        lock_guard<mutex> lock(mtx);

        for (const auto& cliente : clientes) {
            if (cliente.autenticado && cliente.nombre == objetivo) {
                socketObjetivo = cliente.socket;
                ipObjetivo = cliente.ip;
                break;
            }
        }
    }

    if (socketObjetivo == -1) {
        respuesta = "Ese usuario no esta conectado.\n";
        guardarEvento("COMMAND_ERROR", "usuario=" + solicitante + " comando=/expulsar motivo=usuario_no_conectado objetivo=" + objetivo);
        return false;
    }

    string aviso = "Has sido expulsado del chat por un administrador.\n";

    send(socketObjetivo, aviso.c_str(), aviso.size(), 0);
    shutdown(socketObjetivo, SHUT_RDWR);
    close(socketObjetivo);

    respuesta = "Usuario expulsado correctamente: " + objetivo + "\n";

    guardarEvento("KICK", "admin=" + solicitante + " expulsado=" + objetivo + " ip=" + ipObjetivo);

    return true;
}

void cerrarSesionCliente(int socketCliente, const string& nombre, const string& motivo) {
    string ipCliente = obtenerIpCliente(socketCliente);

    string mensajeSalida = "[" + horaActual() + "] *** " + nombre + " salio del chat ***\n";

    cout << mensajeSalida;

    guardarEvento("LOGOUT", "usuario=" + nombre + " ip=" + ipCliente + " motivo=" + motivo);

    close(socketCliente);
    eliminarCliente(socketCliente);

    enviarATodos(mensajeSalida, socketCliente);
    broadcastUsuariosRaw();
}

void manejarCliente(int socketCliente, string ipCliente) {
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    enviarMensaje(socketCliente, "Usuario: ");

    int bytesRecibidos = recv(socketCliente, buffer, sizeof(buffer) - 1, 0);

    if (bytesRecibidos <= 0) {
        guardarEvento("CONNECTION_CLOSED", "ip=" + ipCliente + " etapa=usuario");
        close(socketCliente);
        eliminarCliente(socketCliente);
        return;
    }

    string usuario = trim(string(buffer));

    memset(buffer, 0, sizeof(buffer));
    enviarMensaje(socketCliente, "Contrasena: ");

    bytesRecibidos = recv(socketCliente, buffer, sizeof(buffer) - 1, 0);

    if (bytesRecibidos <= 0) {
        guardarEvento("CONNECTION_CLOSED", "ip=" + ipCliente + " usuario=" + usuario + " etapa=contrasena");
        close(socketCliente);
        eliminarCliente(socketCliente);
        return;
    }

    string contrasena = trim(string(buffer));

    {
        lock_guard<mutex> lock(mtx);

        if (!autenticarUsuario(usuario, contrasena)) {
            enviarMensaje(socketCliente, "LOGIN_FAIL\nAutenticacion fallida.\n");

            guardarEvento("LOGIN_FAIL", "usuario=" + usuario + " ip=" + ipCliente);

            close(socketCliente);

            clientes.erase(
                remove_if(
                    clientes.begin(),
                    clientes.end(),
                    [socketCliente](const Cliente& c) {
                        return c.socket == socketCliente;
                    }
                ),
                clientes.end()
            );

            return;
        }

        if (usuarioConectadoSinLock(usuario)) {
            enviarMensaje(socketCliente, "LOGIN_DUPLICADO\nEse usuario ya esta conectado.\n");

            guardarEvento("LOGIN_DUPLICADO", "usuario=" + usuario + " ip=" + ipCliente);

            close(socketCliente);

            clientes.erase(
                remove_if(
                    clientes.begin(),
                    clientes.end(),
                    [socketCliente](const Cliente& c) {
                        return c.socket == socketCliente;
                    }
                ),
                clientes.end()
            );

            return;
        }

        for (auto& cliente : clientes) {
            if (cliente.socket == socketCliente) {
                cliente.nombre = usuario;
                cliente.autenticado = true;
                break;
            }
        }
    }

    string rolUsuario = obtenerRolUsuario(usuario);

    enviarMensaje(socketCliente, "LOGIN_OK\n");

    string bienvenida = "Bienvenido al chat, " + usuario + " (" + rolUsuario + "). Escribe /ayuda para ver los comandos.\n";
    enviarMensaje(socketCliente, bienvenida);

    string mensajeEntrada = "[" + horaActual() + "] *** " + usuario + " (" + rolUsuario + ") se unio al chat desde " + ipCliente + " ***\n";

    cout << mensajeEntrada;

    guardarEvento("LOGIN_OK", "usuario=" + usuario + " rol=" + rolUsuario + " ip=" + ipCliente);

    enviarATodosIncluyendoEmisor(mensajeEntrada);
    broadcastUsuariosRaw();

    while (true) {
        memset(buffer, 0, sizeof(buffer));

        bytesRecibidos = recv(socketCliente, buffer, sizeof(buffer) - 1, 0);

        if (bytesRecibidos <= 0) {
            string nombre = obtenerNombreCliente(socketCliente);
            cerrarSesionCliente(socketCliente, nombre, "conexion_interrumpida");
            break;
        }

        string mensaje = trim(string(buffer));
        string nombre = obtenerNombreCliente(socketCliente);

        if (mensaje.empty()) {
            guardarEvento("EMPTY_MESSAGE", "usuario=" + nombre + " ip=" + obtenerIpCliente(socketCliente));
            continue;
        }

        if (mensaje == "/salir") {
            cerrarSesionCliente(socketCliente, nombre, "comando_salir");
            break;
        }

        if (mensaje == "/ayuda") {
            enviarMensaje(socketCliente, ayudaComandos());
            guardarEvento("COMMAND", "usuario=" + nombre + " comando=/ayuda");
            continue;
        }

        if (mensaje == "/usuarios") {
            enviarMensaje(socketCliente, listaUsuarios());
            guardarEvento("COMMAND", "usuario=" + nombre + " comando=/usuarios");
            continue;
        }

        if (mensaje.rfind("/msg ", 0) == 0) {
            istringstream iss(mensaje);

            string comando;
            string destino;

            iss >> comando >> destino;

            string texto;
            getline(iss, texto);

            if (!texto.empty() && texto[0] == ' ') {
                texto.erase(0, 1);
            }

            if (destino.empty() || texto.empty()) {
                enviarMensaje(socketCliente, "Uso: /msg usuario mensaje\n");
                guardarEvento("COMMAND_ERROR", "usuario=" + nombre + " comando=/msg motivo=argumentos_incompletos");
                continue;
            }

            bool enviado = enviarPrivado(nombre, destino, texto);

            if (enviado) {
                string confirmacion = "[" + horaActual() + "] [Privado a " + destino + "] " + texto + "\n";
                enviarMensaje(socketCliente, confirmacion);

                guardarEvento("MSG_PRIVADO", "de=" + nombre + " para=" + destino + " longitud=" + to_string(texto.size()));
            } else {
                enviarMensaje(socketCliente, "Usuario no encontrado o no conectado.\n");

                guardarEvento("MSG_PRIVADO_FAIL", "de=" + nombre + " para=" + destino + " motivo=usuario_no_disponible");
            }

            continue;
        }

        if (mensaje.rfind("/registrar ", 0) == 0) {
            istringstream iss(mensaje);

            string comando;
            string nuevoUsuario;
            string nuevaContrasena;
            string nuevoRol;

            iss >> comando >> nuevoUsuario >> nuevaContrasena >> nuevoRol;

            string respuesta;

            bool exito = registrarUsuario(nombre, nuevoUsuario, nuevaContrasena, nuevoRol, respuesta);

            enviarMensaje(socketCliente, respuesta);

            if (exito) {
                string aviso = "[" + horaActual() + "] *** " + nombre + " registro al usuario " + nuevoUsuario + " con rol " + nuevoRol + " ***\n";
                cout << aviso;
            }

            continue;
        }

        if (mensaje.rfind("/expulsar ", 0) == 0) {
            istringstream iss(mensaje);

            string comando;
            string objetivo;

            iss >> comando >> objetivo;

            string respuesta;

            bool exito = expulsarUsuario(nombre, objetivo, respuesta);

            enviarMensaje(socketCliente, respuesta);

            if (exito) {
                string anuncio = "[" + horaActual() + "] *** " + objetivo + " fue expulsado del chat por " + nombre + " ***\n";

                cout << anuncio;

                enviarATodosIncluyendoEmisor(anuncio);
                broadcastUsuariosRaw();
            }

            continue;
        }

        if (!mensaje.empty() && mensaje[0] == '/') {
            enviarMensaje(socketCliente, "Comando no reconocido. Usa /ayuda para ver los comandos disponibles.\n");
            guardarEvento("COMMAND_UNKNOWN", "usuario=" + nombre + " comando=" + mensaje);
            continue;
        }

        string mensajeCompleto = "[" + horaActual() + "] " + nombre + ": " + mensaje + "\n";

        cout << mensajeCompleto;

        guardarEvento("MSG_PUBLICO", "usuario=" + nombre + " longitud=" + to_string(mensaje.size()));

        enviarATodos(mensajeCompleto, socketCliente);
    }
}

int main() {
    int servidor_fd;
    int socketCliente;

    struct sockaddr_in direccionServidor;
    struct sockaddr_in direccionCliente;

    socklen_t tamDireccion = sizeof(direccionCliente);

    cargarUsuarios();

    servidor_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (servidor_fd == -1) {
        cerr << "Error al crear el socket." << endl;
        guardarEvento("ERROR", "No se pudo crear el socket del servidor");
        return 1;
    }

    int opt = 1;

    setsockopt(servidor_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    direccionServidor.sin_family = AF_INET;
    direccionServidor.sin_addr.s_addr = INADDR_ANY;
    direccionServidor.sin_port = htons(5000);

    if (bind(servidor_fd, (struct sockaddr*)&direccionServidor, sizeof(direccionServidor)) < 0) {
        cerr << "Error en bind." << endl;
        guardarEvento("ERROR", "Fallo bind puerto=5000");
        close(servidor_fd);
        return 1;
    }

    if (listen(servidor_fd, 10) < 0) {
        cerr << "Error en listen." << endl;
        guardarEvento("ERROR", "Fallo listen puerto=5000");
        close(servidor_fd);
        return 1;
    }

    cout << "Servidor escuchando puerto 5000" << endl;

    guardarEvento("SERVER_START", "puerto=5000 estado=escuchando");

    while (true) {
        socketCliente = accept(
            servidor_fd,
            (struct sockaddr*)&direccionCliente,
            &tamDireccion
        );

        if (socketCliente < 0) {
            cerr << "Error al aceptar cliente." << endl;
            guardarEvento("ERROR", "Fallo accept");
            continue;
        }

        string ipCliente = inet_ntoa(direccionCliente.sin_addr);

        {
            lock_guard<mutex> lock(mtx);

            clientes.push_back({
                socketCliente,
                "",
                ipCliente,
                false
            });
        }

        cout << "Conexion desde: " << ipCliente << endl;

        guardarEvento("CONNECTION", "ip=" + ipCliente + " estado=nueva_conexion");

        thread hiloCliente(manejarCliente, socketCliente, ipCliente);
        hiloCliente.detach();
    }

    close(servidor_fd);

    guardarEvento("SERVER_STOP", "puerto=5000 estado=detenido");

    return 0;
}
