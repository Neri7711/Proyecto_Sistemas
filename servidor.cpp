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
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

using namespace std;

struct Cliente {
    int socket;
    string nombre;
    string ip;
    bool autenticado;
};

vector<Cliente> clientes;
mutex mtx;
unordered_map<string, string> credenciales;
const string ADMIN = "faisan";

string horaActual() {
    time_t ahora = time(nullptr);
    tm *t = localtime(&ahora);
    char buffer[10];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", t);
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

void guardarLog(const string& mensaje) {
    ofstream archivo("chat.log", ios::app);
    if (archivo.is_open()) {
        archivo << mensaje << endl;
    }
}

void cargarUsuarios() {
    credenciales.clear();
    ifstream archivo("usuarios.txt");
    string linea;

    while (getline(archivo, linea)) {
        size_t pos = linea.find(':');
        if (pos != string::npos) {
            string usuario = linea.substr(0, pos);
            string contrasena = linea.substr(pos + 1);
            if (!usuario.empty() && !contrasena.empty()) {
                credenciales[usuario] = contrasena;
            }
        }
    }
}

bool guardarNuevoUsuario(const string& usuario, const string& contrasena) {
    ofstream archivo("usuarios.txt", ios::app);
    if (!archivo.is_open()) {
        return false;
    }
    archivo << usuario << ":" << contrasena << endl;
    return true;
}

void enviarMensaje(int socketCliente, const string& mensaje) {
    send(socketCliente, mensaje.c_str(), mensaje.size(), 0);
}

bool autenticarUsuario(const string& usuario, const string& contrasena) {
    return credenciales.count(usuario) && credenciales[usuario] == contrasena;
}

bool usuarioConectado(const string& nombre) {
    for (const auto& cliente : clientes) {
        if (cliente.autenticado && cliente.nombre == nombre) {
            return true;
        }
    }
    return false;
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

void eliminarCliente(int socketCliente) {
    lock_guard<mutex> lock(mtx);
    clientes.erase(remove_if(clientes.begin(), clientes.end(),
        [socketCliente](const Cliente& c) {
            return c.socket == socketCliente;
        }), clientes.end());
}

string listaUsuarios() {
    lock_guard<mutex> lock(mtx);
    string resultado = "Usuarios conectados:\n";
    bool hayUsuarios = false;

    for (const auto& cliente : clientes) {
        if (cliente.autenticado) {
            resultado += "- " + cliente.nombre + "\n";
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

bool registrarUsuario(const string& solicitante, const string& nuevoUsuario, const string& nuevaContrasena, string& respuesta) {
    lock_guard<mutex> lock(mtx);

    if (solicitante != ADMIN) {
        respuesta = "No tienes permisos para registrar usuarios.\n";
        return false;
    }

    if (nuevoUsuario.empty() || nuevaContrasena.empty()) {
        respuesta = "Uso: /registrar usuario contrasena\n";
        return false;
    }

    if (nuevoUsuario.find(':') != string::npos || nuevoUsuario.find(' ') != string::npos) {
        respuesta = "El usuario contiene caracteres invalidos.\n";
        return false;
    }

    if (nuevaContrasena.find(':') != string::npos || nuevaContrasena.find(' ') != string::npos) {
        respuesta = "La contrasena contiene caracteres invalidos.\n";
        return false;
    }

    if (credenciales.count(nuevoUsuario)) {
        respuesta = "Ese usuario ya existe.\n";
        return false;
    }

    if (!guardarNuevoUsuario(nuevoUsuario, nuevaContrasena)) {
        respuesta = "No se pudo guardar el nuevo usuario en usuarios.txt\n";
        return false;
    }

    credenciales[nuevoUsuario] = nuevaContrasena;
    respuesta = "Usuario registrado correctamente: " + nuevoUsuario + "\n";
    return true;
}

bool expulsarUsuario(const string& solicitante, const string& objetivo, string& respuesta) {
    if (solicitante != ADMIN) {
        respuesta = "No tienes permisos para expulsar usuarios.\n";
        return false;
    }

    if (objetivo.empty()) {
        respuesta = "Uso: /expulsar usuario\n";
        return false;
    }

    if (objetivo == ADMIN) {
        respuesta = "No puedes expulsarte a ti mismo.\n";
        return false;
    }

    int socketObjetivo = -1;

    {
        lock_guard<mutex> lock(mtx);
        for (const auto& cliente : clientes) {
            if (cliente.autenticado && cliente.nombre == objetivo) {
                socketObjetivo = cliente.socket;
                break;
            }
        }
    }

    if (socketObjetivo == -1) {
        respuesta = "Ese usuario no esta conectado.\n";
        return false;
    }

    string aviso = "Has sido expulsado del chat por el administrador.\n";
    send(socketObjetivo, aviso.c_str(), aviso.size(), 0);
    shutdown(socketObjetivo, SHUT_RDWR);
    close(socketObjetivo);

    respuesta = "Usuario expulsado correctamente: " + objetivo + "\n";
    return true;
}

void manejarCliente(int socketCliente, string ipCliente) {
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    enviarMensaje(socketCliente, "Usuario: ");
    int bytesRecibidos = recv(socketCliente, buffer, sizeof(buffer) - 1, 0);
    if (bytesRecibidos <= 0) {
        close(socketCliente);
        eliminarCliente(socketCliente);
        return;
    }

    string usuario = trim(string(buffer));

    memset(buffer, 0, sizeof(buffer));
    enviarMensaje(socketCliente, "Contrasena: ");
    bytesRecibidos = recv(socketCliente, buffer, sizeof(buffer) - 1, 0);
    if (bytesRecibidos <= 0) {
        close(socketCliente);
        eliminarCliente(socketCliente);
        return;
    }

    string contrasena = trim(string(buffer));

    {
        lock_guard<mutex> lock(mtx);

        if (!autenticarUsuario(usuario, contrasena)) {
            enviarMensaje(socketCliente, "LOGIN_FAIL\nAutenticacion fallida.\n");
            close(socketCliente);
            clientes.erase(remove_if(clientes.begin(), clientes.end(),
                [socketCliente](const Cliente& c) { return c.socket == socketCliente; }),
                clientes.end());
            return;
        }

        if (usuarioConectado(usuario)) {
            enviarMensaje(socketCliente, "LOGIN_DUPLICADO\nEse usuario ya esta conectado.\n");
            close(socketCliente);
            clientes.erase(remove_if(clientes.begin(), clientes.end(),
                [socketCliente](const Cliente& c) { return c.socket == socketCliente; }),
                clientes.end());
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

    enviarMensaje(socketCliente, "LOGIN_OK\n");
    string mensajeEntrada = "[" + horaActual() + "] *** " + usuario + " se unio al chat desde " + ipCliente + " ***\n";
    cout << mensajeEntrada;
    guardarLog(mensajeEntrada.substr(0, mensajeEntrada.size() - 1));
    enviarATodosIncluyendoEmisor(mensajeEntrada);
    broadcastUsuariosRaw();

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        bytesRecibidos = recv(socketCliente, buffer, sizeof(buffer) - 1, 0);

        if (bytesRecibidos <= 0) {
            string nombre = obtenerNombreCliente(socketCliente);
            string mensajeSalida = "[" + horaActual() + "] *** " + nombre + " salio del chat ***\n";
            cout << mensajeSalida;
            guardarLog(mensajeSalida.substr(0, mensajeSalida.size() - 1));
            close(socketCliente);
            eliminarCliente(socketCliente);
            enviarATodos(mensajeSalida, socketCliente);
            broadcastUsuariosRaw();
            break;
        }

        string mensaje = trim(string(buffer));
        string nombre = obtenerNombreCliente(socketCliente);

        if (mensaje == "/salir") {
            string mensajeSalida = "[" + horaActual() + "] *** " + nombre + " salio del chat ***\n";
            cout << mensajeSalida;
            guardarLog(mensajeSalida.substr(0, mensajeSalida.size() - 1));
            close(socketCliente);
            eliminarCliente(socketCliente);
            enviarATodos(mensajeSalida, socketCliente);
            broadcastUsuariosRaw();
            break;
        }

        if (mensaje == "/usuarios") {
            enviarMensaje(socketCliente, listaUsuarios());
            continue;
        }

        if (mensaje.rfind("/msg ", 0) == 0) {
            istringstream iss(mensaje);
            string comando, destino;
            iss >> comando >> destino;
            string texto;
            getline(iss, texto);

            if (!texto.empty() && texto[0] == ' ') {
                texto.erase(0, 1);
            }

            if (destino.empty() || texto.empty()) {
                enviarMensaje(socketCliente, "Uso: /msg usuario mensaje\n");
                continue;
            }

            bool enviado = enviarPrivado(nombre, destino, texto);

            if (enviado) {
                string confirmacion = "[" + horaActual() + "] [Privado a " + destino + "] " + texto + "\n";
                enviarMensaje(socketCliente, confirmacion);
                guardarLog("[" + horaActual() + "] [Privado] " + nombre + " -> " + destino + ": " + texto);
            } else {
                enviarMensaje(socketCliente, "Usuario no encontrado o no conectado.\n");
            }
            continue;
        }

        if (mensaje.rfind("/registrar ", 0) == 0) {
            istringstream iss(mensaje);
            string comando, nuevoUsuario, nuevaContrasena;
            iss >> comando >> nuevoUsuario >> nuevaContrasena;

            string respuesta;
            bool exito = registrarUsuario(nombre, nuevoUsuario, nuevaContrasena, respuesta);
            enviarMensaje(socketCliente, respuesta);

            if (exito) {
                guardarLog("[" + horaActual() + "] [ADMIN] " + nombre + " registro al usuario: " + nuevoUsuario);
            }
            continue;
        }

        if (mensaje.rfind("/expulsar ", 0) == 0) {
            istringstream iss(mensaje);
            string comando, objetivo;
            iss >> comando >> objetivo;

            string respuesta;
            bool exito = expulsarUsuario(nombre, objetivo, respuesta);
            enviarMensaje(socketCliente, respuesta);

            if (exito) {
                string anuncio = "[" + horaActual() + "] *** " + objetivo + " fue expulsado del chat por " + nombre + " ***\n";
                cout << anuncio;
                guardarLog(anuncio.substr(0, anuncio.size() - 1));
                enviarATodosIncluyendoEmisor(anuncio);
                broadcastUsuariosRaw();
            }
            continue;
        }

        string mensajeCompleto = "[" + horaActual() + "] " + nombre + ": " + mensaje + "\n";
        cout << mensajeCompleto;
        guardarLog(mensajeCompleto.substr(0, mensajeCompleto.size() - 1));
        enviarATodos(mensajeCompleto, socketCliente);
    }
}

int main() {
    int servidor_fd, socketCliente;
    struct sockaddr_in direccionServidor, direccionCliente;
    socklen_t tamDireccion = sizeof(direccionCliente);

    cargarUsuarios();

    servidor_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor_fd == -1) {
        cerr << "Error al crear el socket." << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(servidor_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    direccionServidor.sin_family = AF_INET;
    direccionServidor.sin_addr.s_addr = INADDR_ANY;
    direccionServidor.sin_port = htons(5000);

    if (bind(servidor_fd, (struct sockaddr*)&direccionServidor, sizeof(direccionServidor)) < 0) {
        cerr << "Error en bind." << endl;
        close(servidor_fd);
        return 1;
    }

    if (listen(servidor_fd, 10) < 0) {
        cerr << "Error en listen." << endl;
        close(servidor_fd);
        return 1;
    }

    cout << "Servidor de chat escuchando en el puerto 5000..." << endl;

    while (true) {
        socketCliente = accept(servidor_fd, (struct sockaddr*)&direccionCliente, &tamDireccion);
        if (socketCliente < 0) {
            cerr << "Error al aceptar cliente." << endl;
            continue;
        }

        string ipCliente = inet_ntoa(direccionCliente.sin_addr);

        {
            lock_guard<mutex> lock(mtx);
            clientes.push_back({socketCliente, "", ipCliente, false});
        }

        cout << "Nueva conexion desde: " << ipCliente << endl;
        thread hiloCliente(manejarCliente, socketCliente, ipCliente);
        hiloCliente.detach();
    }

    close(servidor_fd);
    return 0;
}
