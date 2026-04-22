#include <iostream>
#include <thread>
#include <string>
#include <cstring>
#include <vector>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <readline/readline.h>
#include <readline/history.h>

using namespace std;

int g_socketCliente = -1;
mutex g_usersMutex;
vector<string> g_connectedUsers;
atomic<bool> g_running(true);

vector<string> g_commands = {
    "/usuarios",
    "/msg",
    "/registrar",
    "/expulsar",
    "/salir"
};

bool startsWith(const string& s, const string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool recibirTexto(int socketCliente, string& salida) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(socketCliente, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        return false;
    }
    salida = buffer;
    return true;
}

void actualizarUsuariosDesdeRaw(const string& respuesta) {
    if (!startsWith(respuesta, "USERS_LIST:")) {
        return;
    }

    string lista = respuesta.substr(string("USERS_LIST:").size());
    lista.erase(remove(lista.begin(), lista.end(), '\n'), lista.end());
    lista.erase(remove(lista.begin(), lista.end(), '\r'), lista.end());

    vector<string> nuevos;
    string item;
    stringstream ss(lista);

    while (getline(ss, item, ',')) {
        if (!item.empty()) {
            nuevos.push_back(item);
        }
    }

    lock_guard<mutex> lock(g_usersMutex);
    g_connectedUsers = nuevos;
}

char* duplicarCadena(const string& s) {
    char* resultado = (char*)malloc(s.size() + 1);
    strcpy(resultado, s.c_str());
    return resultado;
}

char* command_generator(const char* text, int state) {
    static size_t index;
    static vector<string> matches;

    if (state == 0) {
        index = 0;
        matches.clear();
        string pref = text;

        for (const auto& cmd : g_commands) {
            if (cmd.find(pref) == 0) {
                matches.push_back(cmd);
            }
        }
    }

    if (index < matches.size()) {
        return duplicarCadena(matches[index++]);
    }

    return nullptr;
}

char* user_generator(const char* text, int state) {
    static size_t index;
    static vector<string> matches;

    if (state == 0) {
        index = 0;
        matches.clear();
        string pref = text;

        lock_guard<mutex> lock(g_usersMutex);
        for (const auto& user : g_connectedUsers) {
            if (user.find(pref) == 0) {
                matches.push_back(user);
            }
        }
    }

    if (index < matches.size()) {
        return duplicarCadena(matches[index++]);
    }

    return nullptr;
}

char** completion_router(const char* text, int start, int end) {
    (void)end;

    string buffer = rl_line_buffer ? rl_line_buffer : "";

    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }

    if (startsWith(buffer, "/msg ") && start >= 5) {
        return rl_completion_matches(text, user_generator);
    }

    if (startsWith(buffer, "/expulsar ") && start >= 10) {
        return rl_completion_matches(text, user_generator);
    }

    return nullptr;
}

void recibirMensajes() {
    char buffer[4096];

    while (g_running) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(g_socketCliente, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            g_running = false;
            rl_replace_line("", 0);
            rl_on_new_line();
            cout << "\nDesconectado del servidor." << endl;
            rl_redisplay();
            break;
        }

        string mensaje = buffer;

        if (startsWith(mensaje, "USERS_LIST:")) {
            actualizarUsuariosDesdeRaw(mensaje);
            rl_on_new_line();
            rl_redisplay();
            continue;
        }

        rl_save_prompt();
        rl_replace_line("", 0);
        rl_crlf();

        cout << mensaje;
        if (!mensaje.empty() && mensaje.back() != '\n') {
            cout << endl;
        }

        rl_restore_prompt();
        rl_on_new_line();
        rl_redisplay();
    }
}

int main() {
    int socketCliente;
    struct sockaddr_in direccionServidor;

    socketCliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socketCliente == -1) {
        cerr << "Error al crear socket." << endl;
        return 1;
    }

    g_socketCliente = socketCliente;

    direccionServidor.sin_family = AF_INET;
    direccionServidor.sin_port = htons(5000);

    if (inet_pton(AF_INET, "10.15.216.84", &direccionServidor.sin_addr) <= 0) {
        cerr << "Direccion IP invalida." << endl;
        close(socketCliente);
        return 1;
    }

    if (connect(socketCliente, (struct sockaddr*)&direccionServidor, sizeof(direccionServidor)) < 0) {
        cerr << "No se pudo conectar al servidor." << endl;
        close(socketCliente);
        return 1;
    }

    string entrada, respuesta;

    if (!recibirTexto(socketCliente, respuesta)) {
        cerr << "Error al recibir solicitud de usuario." << endl;
        close(socketCliente);
        return 1;
    }
    cout << respuesta;
    getline(cin, entrada);
    send(socketCliente, entrada.c_str(), entrada.size(), 0);

    if (!recibirTexto(socketCliente, respuesta)) {
        cerr << "Error al recibir solicitud de contrasena." << endl;
        close(socketCliente);
        return 1;
    }
    cout << respuesta;
    getline(cin, entrada);
    send(socketCliente, entrada.c_str(), entrada.size(), 0);

    if (!recibirTexto(socketCliente, respuesta)) {
        cerr << "No hubo respuesta del servidor tras el login." << endl;
        close(socketCliente);
        return 1;
    }

    if (respuesta.rfind("LOGIN_OK", 0) == 0) {
        cout << "Autenticacion exitosa." << endl;
    } else if (respuesta.rfind("LOGIN_FAIL", 0) == 0) {
        cout << respuesta.substr(string("LOGIN_FAIL\n").size());
        close(socketCliente);
        return 1;
    } else if (respuesta.rfind("LOGIN_DUPLICADO", 0) == 0) {
        cout << respuesta.substr(string("LOGIN_DUPLICADO\n").size());
        close(socketCliente);
        return 1;
    } else {
        cout << respuesta;
    }

    rl_attempted_completion_function = completion_router;
    using_history();

    thread hiloRecibir(recibirMensajes);
    hiloRecibir.detach();

    while (g_running) {
        char* linea = readline("> ");
        if (!linea) {
            g_running = false;
            shutdown(socketCliente, SHUT_RDWR);
            close(socketCliente);
            return 0;
        }

        string mensaje(linea);
        free(linea);

        if (mensaje.empty()) {
            continue;
        }

        add_history(mensaje.c_str());

        ssize_t enviados = send(socketCliente, mensaje.c_str(), mensaje.size(), 0);
        if (enviados <= 0) {
            g_running = false;
            shutdown(socketCliente, SHUT_RDWR);
            close(socketCliente);
            return 0;
        }

        if (mensaje == "/salir") {
            g_running = false;
            shutdown(socketCliente, SHUT_RDWR);
            close(socketCliente);
            return 0;
        }
    }

    shutdown(socketCliente, SHUT_RDWR);
    close(socketCliente);
    return 0;
}
