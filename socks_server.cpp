#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdio.h>


using boost::asio::ip::tcp;
boost::asio::io_context io_context;

class socks_server {
 public:
  socks_server(uint16_t port)
      : _acceptor(io_context, {tcp::v4(), port})
  {
    do_accept();
  }

 private:
  void do_accept() {
    _acceptor.async_accept(
        [this](boost::system::error_code ec, tcp::socket new_socket) {
          if (!ec) {
            _browser_socket = std::move(new_socket);
            io_context.notify_fork(boost::asio::io_context::fork_prepare);

            int pid;
            if ((pid = fork()) == -1){
              std::cerr << "fork: " << strerror(errno) << std::endl;
              exit(0);
            }
            if (pid == 0) { //child
              io_context.notify_fork(boost::asio::io_context::fork_child);
              _acceptor.close();
              do_read_req();

            } else { //paraent
              io_context.notify_fork(boost::asio::io_context::fork_parent);
              _browser_socket.close();
              do_accept();
            }
          } else {
            std::cerr << "Accept error: " << ec.message() << std::endl;
            do_accept();

          }
        });
  }

  void do_read_req() {
    boost::asio::async_read_until(_browser_socket,
        boost::asio::dynamic_buffer(input_buffer_), '\0',
        [this](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            std::cout << "length: " << length << std::endl;
            for (size_t i = 0; i < input_buffer_.size(); i++){
              printf("%d\n", input_buffer_[i]);
            }
            std::cout << "socks4 size: " << sizeof(req) << std::endl;
            req.VN = input_buffer_[0];
            req.CD = input_buffer_[1];
            req.DSTPORT = input_buffer_[2] << 8 |
                          input_buffer_[3];
            req.DSTIP[0] = input_buffer_[4];
            req.DSTIP[1] = input_buffer_[5];
            req.DSTIP[2] = input_buffer_[6];
            req.DSTIP[3] = input_buffer_[7];
            
            SRCIP = _browser_socket.remote_endpoint().address().to_string();
            SRCPORT = _browser_socket.remote_endpoint().port();
            std::cout << "req.VN: " << (int)req.VN << std::endl;
            std::cout << "req.CD: " << (int)req.CD << std::endl;
            std::cout << "<S_IP>: " << SRCIP << std::endl;
            std::cout << "<S_PORT>: " << SRCPORT << std::endl;
            std::cout << "<D_IP>: " << IPtoStr(req.DSTIP) << std::endl;
            std::cout << "<D_PORT>: " << req.DSTPORT << std::endl;
            if (req.CD == CD_CONN){
              std::cout << "<Command>: " << "CONNECT" << std::endl;
            }else if (req.CD == CD_BIND){
              std::cout << "<Command>: " << "BIND" << std::endl;
            }
            // match socks.conf
            // if accept
            output_buffer_.clear();
            rep.VN = (unsigned char) 0;
            rep.CD = (unsigned char) CD_ACCP;
            output_buffer_.push_back(rep.VN);
            output_buffer_.push_back(rep.CD);
            for (int i = 0; i < 6; i++){
              output_buffer_.push_back(0);
            }
            do_write_rep();
          }
        });
  }
  void do_write_rep() {
    
  }

  tcp::acceptor _acceptor{io_context};
  tcp::resolver _resolver{io_context};
  tcp::socket _browser_socket{io_context};
  tcp::socket _service_socket{io_context};
  /*
  enum { max_length = 512 };
  char rdata_[max_length];
  char wdata_[max_length];
  */
  std::vector<unsigned char> input_buffer_;
  std::vector<unsigned char> output_buffer_;
  struct socks4{
    unsigned char VN;
    unsigned char CD;
    unsigned short DSTPORT; // 2 byte
    unsigned char DSTIP[4]; // 4 byte
    unsigned char nullByte = '\0';
  };
  enum {
    CD_CONN = 1,
    CD_BIND = 2,
    CD_ACCP = 90,
    CD_RJCT = 91
  };
  struct socks4 req, rep;
  unsigned short SRCPORT;
  std::string SRCIP;
  // util
  std::string IPtoStr(unsigned char* iparr){
    char buffer [16];
    sprintf(buffer, "%d.%d.%d.%d", iparr[0], iparr[1], iparr[2], iparr[3]);
    return std::string(buffer);
  }
};

void childHandler(int sig){
  while(waitpid(-1, NULL, WNOHANG) > 0){
  }
}

int main(int argc, char *argv[]) {
  using namespace std;
  signal (SIGCHLD, childHandler);
  
  try {
    if (argc!=2) {
      std::cerr << "Usage: ./socks_server <port>\n";
      return 1;
    }
    socks_server s(atoi(argv[1]));
    io_context.run();
  }
  catch (exception &e) {
    cerr << "Exception: " << e.what() << endl;
  }
}