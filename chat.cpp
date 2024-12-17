#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <boost/asio/ip/address.hpp>
#include <curl/curl.h> // For public IP retrieval
#include <ifaddrs.h>
#include <arpa/inet.h>

using boost::asio::ip::udp;

// Utility function to get public IP address using an external service
std::string get_public_ip() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "cURL initialization failed." << std::endl;
        return "";
    }

    std::string public_ip;
    CURLcode res;

    // Set cURL options
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.ipify.org");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        std::string* data = static_cast<std::string*>(userdata);
        data->append(ptr, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &public_ip);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ChatApplication/1.0"); // Optional: Set a custom user agent
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Set a timeout of 10 seconds

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        std::cerr << "cURL error: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }

    // Verify that the response is a valid IP address
    boost::system::error_code ec;
    boost::asio::ip::address addr = boost::asio::ip::make_address(public_ip, ec);
    if (ec) {
        std::cerr << "Invalid IP address format received: " << public_ip << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }

    curl_easy_cleanup(curl);
    return public_ip;
}

// Function to automatically retrieve the internal IP address
std::string get_internal_ip() {
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];
    std::string internal_ip = "0.0.0.0"; // Default IP

    if (getifaddrs(&ifaddr) == -1) {
        std::cerr << "Error retrieving network interfaces." << std::endl;
        return internal_ip;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) { // IPv4
            // Skip loopback interface
            if (std::string(ifa->ifa_name) == "lo0")
                continue;

            int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                                host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s == 0) {
                internal_ip = host;
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
    return internal_ip;
}

// Shared variables for host to store client endpoint
std::mutex peer_mutex;
udp::endpoint peer_endpoint;
bool peer_set = false;
std::condition_variable peer_cv;

// Atomic flag to signal termination
std::atomic<bool> terminate_flag(false);

// Function to listen for incoming messages
void listen_for_messages(udp::socket& socket, bool is_host) {
    char buffer[1024];
    udp::endpoint sender_endpoint;

    while (!terminate_flag.load()) {
        boost::system::error_code error;
        size_t bytes_received = socket.receive_from(
            boost::asio::buffer(buffer), sender_endpoint, 0, error);

        if (!error && bytes_received > 0) {
            std::string message(buffer, bytes_received);
            std::cout << "\n[Message from " << sender_endpoint.address().to_string()
                      << ":" << sender_endpoint.port() << "] " << message << std::endl;

            if (is_host) {
                // If peer not set yet, set the client endpoint
                std::unique_lock<std::mutex> lock(peer_mutex);
                if (!peer_set) {
                    peer_endpoint = sender_endpoint;
                    peer_set = true;
                    lock.unlock();
                    peer_cv.notify_one();
                }
            }
        } else {
            if (error != boost::asio::error::operation_aborted) {
                std::cerr << "Error receiving message: " << error.message() << std::endl;
            }
        }
    }
}

// Function to send messages (Sender Loop)
void send_messages(udp::socket& socket, bool is_host, udp::endpoint host_endpoint = udp::endpoint()) {
    while (!terminate_flag.load()) {
        std::string message;
        std::getline(std::cin, message);

        if (message.empty())
            continue;

        if (message == "/quit") {
            terminate_flag.store(true);
            socket.cancel(); // Interrupt the listener thread
            break;
        }

        boost::system::error_code ignored_error;
        if (is_host) {
            // Wait until peer_endpoint is set
            std::unique_lock<std::mutex> lock(peer_mutex);
            peer_cv.wait(lock, [] { return peer_set || terminate_flag.load(); });

            if (peer_set) {
                socket.send_to(boost::asio::buffer(message), peer_endpoint, 0, ignored_error);
            } else {
                std::cerr << "No client connected to send messages.\n";
            }
        } else {
            // Client knows host's endpoint
            socket.send_to(boost::asio::buffer(message), host_endpoint, 0, ignored_error);
        }
    }
}

int main() {
    try {
        boost::asio::io_context io_context;

        std::cout << "Are you the host? (y/n): ";
        char is_host_char;
        std::cin >> is_host_char;
        std::cin.ignore(); // Ignore leftover newline

        bool is_host = (is_host_char == 'y' || is_host_char == 'Y');

        unsigned short start_port = 5000;
        unsigned short end_port = 5999;
        udp::socket socket(io_context, udp::endpoint(udp::v4(), 0));

        static std::set<unsigned short> used_ports;
        unsigned short assigned_port = 0;

        // Assign an available port dynamically
        for (unsigned short port = start_port; port <= end_port; ++port) {
            if (used_ports.find(port) == used_ports.end()) {
                assigned_port = port;
                used_ports.insert(port);
                break;
            }
        }

        if (assigned_port == 0) {
            std::cerr << "No available port found in the 5000-5999 range." << std::endl;
            return 1;
        }

        bool bind_success = false;
        while (!bind_success) {
            try {
                socket.close();
                socket.open(udp::v4());
                socket.bind(udp::endpoint(udp::v4(), assigned_port));
                bind_success = true;
            } catch (const boost::system::system_error& e) {
                if (e.code() == boost::system::errc::address_in_use) {
                    std::cerr << "Port " << assigned_port << " is already in use. Trying the next port...\n";
                    ++assigned_port;
                    if (assigned_port > end_port) {
                        std::cerr << "No available port found in the range." << std::endl;
                        return 1;
                    }
                } else {
                    std::cerr << "Error binding socket: " << e.what() << std::endl;
                    return 1;
                }
            }
        }

        std::cout << "Assigned port: " << assigned_port << std::endl;

        udp::endpoint host_endpoint; // For client to store host's endpoint

        if (is_host) {
            std::cout << "Fetching public IP address...\n";
            std::string host_ip = get_public_ip();
            std::cout << "Public IP fetched: " << host_ip << std::endl;

            if (host_ip.empty()) {
                std::cerr << "Failed to get public IP address." << std::endl;
                // Proceeding without public IP
            }

            std::string internal_ip = get_internal_ip();
            if (internal_ip == "0.0.0.0") {
                std::cerr << "Failed to retrieve internal IP address." << std::endl;
            } else {
                std::cout << "Your internal endpoint (for local network): " << internal_ip << ":" << assigned_port << std::endl;
            }

            if (!host_ip.empty()) {
                std::cout << "Your public endpoint (share this with the other player): "
                          << host_ip << ":" << assigned_port << std::endl;
            }

            if (host_ip.empty()) {
                std::cout << "Proceeding without public endpoint. Ensure port " << assigned_port
                          << " is forwarded manually if connecting over the internet.\n";
            }

            std::cout << "Waiting for the client to connect...\n";
        } else {
            std::string peer_ip;
            unsigned short peer_port;
            std::cout << "Do you want to connect using (1) Public IP or (2) Internal IP? Enter 1 or 2: ";
            int choice;
            std::cin >> choice;
            std::cin.ignore();  // Ignore any leftover newline character

            // Input Validation Loop for IP Address
            while (true) {
                if (choice == 1) {
                    std::cout << "Enter the host's public IP address: ";
                } else if (choice == 2) {
                    std::cout << "Enter the host's internal IP address: ";
                } else {
                    std::cerr << "Invalid choice. Please enter 1 for Public IP or 2 for Internal IP: ";
                    std::cin >> choice;
                    std::cin.ignore();
                    continue;
                }

                std::cin >> peer_ip;

                // Validate IP Address
                boost::system::error_code ec;
                boost::asio::ip::address addr = boost::asio::ip::make_address(peer_ip, ec);
                if (ec) {
                    std::cerr << "Invalid IP address format. Please try again.\n";
                    continue;
                }

                host_endpoint = udp::endpoint(addr, 0); // Port will be set below
                break;
            }

            std::cout << "Enter the host's port: ";
            while (!(std::cin >> peer_port) || peer_port < 1 || peer_port > 65535) {
                std::cerr << "Invalid port number. Please enter a value between 1 and 65535: ";
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
            std::cin.ignore();  // Ignore any leftover newline character

            host_endpoint.port(peer_port); // Set the correct port

            try {
                // Send a test message to confirm connection
                std::string test_message = "Hello from client!";
                socket.send_to(boost::asio::buffer(test_message), host_endpoint);
                std::cout << "Test message sent to " << peer_ip << ":" << peer_port << std::endl;
            } catch (const boost::system::system_error& e) {
                std::cerr << "Error creating endpoint or sending message: " << e.what() << std::endl;
                return 1;
            }
        }

        // Start listener thread
        std::thread listener_thread(listen_for_messages, std::ref(socket), is_host);

        // Sending loop (Main Thread)
        if (is_host) {
            // Host needs to wait until a client connects
            std::unique_lock<std::mutex> lock(peer_mutex);
            peer_cv.wait(lock, [] { return peer_set || terminate_flag.load(); });
            lock.unlock();

            if (terminate_flag.load()) {
                listener_thread.join();
                return 0;
            }

            std::cout << "You can now start sending messages. Type '/quit' to exit.\n";
            send_messages(socket, is_host);
        } else {
            // Client already knows host's endpoint
            std::cout << "You can now start sending messages. Type '/quit' to exit.\n";
            send_messages(socket, is_host, host_endpoint);
        }

        // Signal listener thread to terminate
        terminate_flag.store(true);
        socket.cancel(); // Interrupt the listener thread if it's blocked

        // Join the listener thread
        if (listener_thread.joinable()) {
            listener_thread.join();
        }

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}