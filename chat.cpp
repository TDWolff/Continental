#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <thread>

using boost::asio::ip::udp;

// Function to listen for incoming messages
void listen_for_messages(udp::socket &socket) {
    char buffer[1024];
    udp::endpoint sender_endpoint;

    while (true) {
        // Receive messages
        boost::system::error_code error;
        size_t bytes_received = socket.receive_from(
            boost::asio::buffer(buffer), sender_endpoint, 0, error);

        if (!error) {
            // Print received message
            std::cout << "\n[Message from " << sender_endpoint.address().to_string()
                      << ":" << sender_endpoint.port() << "] "
                      << std::string(buffer, bytes_received) << std::endl;
        }
    }
}

int main() {
    try {
        boost::asio::io_context io_context;

        // Get user input for port and peer details
        std::cout << "Enter your port: ";
        unsigned short local_port;
        std::cin >> local_port;

        std::cout << "Enter peer's IP: ";
        std::string peer_ip;
        std::cin >> peer_ip;

        std::cout << "Enter peer's port: ";
        unsigned short peer_port;
        std::cin >> peer_port;

        udp::endpoint local_endpoint(udp::v4(), local_port);
        udp::endpoint peer_endpoint(boost::asio::ip::address::from_string(peer_ip), peer_port);

        udp::socket socket(io_context, local_endpoint);

        // Start a thread to listen for incoming messages
        std::thread listener_thread(listen_for_messages, std::ref(socket));

        std::cout << "You can now type messages. Type 'exit' to quit.\n";

        // Main loop for sending messages
        std::string message;
        while (true) {
            std::getline(std::cin, message);

            if (message == "exit") {
                break;
            }

            // Send the message to the peer
            boost::system::error_code error;
            socket.send_to(boost::asio::buffer(message), peer_endpoint, 0, error);

            if (error) {
                std::cerr << "Failed to send message: " << error.message() << std::endl;
            }
        }

        // Clean up
        listener_thread.detach();
        socket.close();
    } catch (std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
