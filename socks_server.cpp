#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdio.h>
#include <algorithm>


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
    _browser_socket.async_read_some(
        boost::asio::buffer(input_buffer_, max_length),
        [this](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            std::cout << "read some length: " << length << std::endl;
            
            for (size_t i = 0; i < length; i++){
              printf("%d\n", input_buffer_[i]);
            }
            
            //std::cout << "socks4 size: " << sizeof(req) << std::endl;
            req.VN = input_buffer_[0];
            req.CD = input_buffer_[1];
            req.DSTPORT = input_buffer_[2] << 8 |
                          input_buffer_[3];
            req.DSTIP[0] = input_buffer_[4];
            req.DSTIP[1] = input_buffer_[5];
            req.DSTIP[2] = input_buffer_[6];
            req.DSTIP[3] = input_buffer_[7];
            /*
            if(req.DSTIP[0] != 140 || req.DSTIP[1] != 113){
              std::cout << "reject: " << IPtoStr(req.DSTIP) << std::endl;
              _browser_socket.close();
              exit(0);
            }
            */
           
            if (isSocks4a()){
              bool foundNull = false;
              for (std::size_t i = 8; i < length; i++){
                if (input_buffer_[i] == 0 && foundNull == false){
                  foundNull = true;
                  continue;
                }
                if (foundNull == true){
                  unresolveDst.push_back(input_buffer_[i]);
                }
              }
              if (unresolveDst.size()!=0){
                std::cout << "<D_DOMAIN>: " << unresolveDst << std::endl;
              }
              
            }
            

            SRCIP = _browser_socket.remote_endpoint().address().to_string();
            SRCPORT = _browser_socket.remote_endpoint().port();
            //std::cout << "req.VN: " << (int)req.VN << std::endl;
            //std::cout << "req.CD: " << (int)req.CD << std::endl;
            std::cout << "<S_IP>: " << SRCIP << std::endl;
            std::cout << "<S_PORT>: " << SRCPORT << std::endl;
            
            // resolve
            if (isSocks4a()){
              do_resolve(unresolveDst);
            }else{
              do_resolve(IPtoStr(req.DSTIP));
            }
            
            
            

            // if accept
            // build connection to service server

            
          }
        });
  }

  void do_resolve(std::string ipOrUrl) {
    _resolver.async_resolve(ipOrUrl, std::to_string(req.DSTPORT),
      [this](boost::system::error_code ec,
      tcp::resolver::results_type returned_endpoints)
      {
        if (!ec)
        {
          endpoints = returned_endpoints;
          tcp::resolver::results_type::iterator endpoint_iter = endpoints.begin();
          while(endpoint_iter != endpoints.end())
          {
            std::cout << "Trying " << endpoint_iter->endpoint() << "\n";
            endpoint_iter++;
            // TODO ************************************************
            // match socks.conf
          }
          do_connect();
        }
        else{
          std::cout << "resolve Error: " << ec.message() << "\n";
        }
      });
  }

  void do_connect() { // get socket after connect
    boost::asio::async_connect(_service_socket, endpoints,
    [this](boost::system::error_code ec, tcp::endpoint endpoint){
      if (!ec)
      {
        std::cout << "Done service connection" << std::endl;
        
        // print remaining info
        std::cout << "<D_IP>: " << endpoint.address().to_string() << std::endl;
        std::cout << "<D_PORT>: " << req.DSTPORT << std::endl;
        if (req.CD == CD_CONN){
          std::cout << "<Command>: CONNECT" << std::endl;
        }else if (req.CD == CD_BIND){
          std::cout << "<Command>: BIND" << std::endl;
        }

        // send reply
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
      else{
        std::cout << "connect Error: " << ec.message() << "\n";
      }
    });
  }
  
  void do_write_rep() {
    boost::asio::async_write(_browser_socket, boost::asio::buffer(output_buffer_, output_buffer_.size()),
        [this](boost::system::error_code ec, std::size_t )
        {
          if (!ec)
          {
            std::cout << "Done socks4 reply" << std::endl;
            do_read_http_req();
            do_read_http_rep();
          }
        });
  }
  void do_read_http_req() {
    _browser_socket.async_read_some(
        boost::asio::buffer(rdata_, max_length-1),
        [this](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            rdata_[length+1] = '\0';
            std::cout << "(raw recv "<< length <<")" << rdata_ << std::endl;
            std::string reply = std::string(rdata_);
            std::cout << "(recv)" << reply << std::flush;
            do_write_http_req(length);
          }
        });
  }
  void do_write_http_req(std::size_t length){
    boost::asio::async_write(_service_socket, boost::asio::buffer(rdata_, length),
        [this](boost::system::error_code ec, std::size_t )
        {
          if (!ec)
          {
            std::cout << "Done passing http request" << std::endl;
            memset(rdata_, '\0', max_length);
            do_read_http_req();
          }
        });
  }
  void do_read_http_rep(){
    _service_socket.async_read_some(
        boost::asio::buffer(rdata_, max_length-1),
        [this](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            rdata_[length+1] = '\0';
            std::cout << "(raw recv "<< length <<")" << rdata_ << std::endl;
            std::string reply = std::string(rdata_);
            std::cout << "(recv)" << reply << std::flush;
            do_write_http_rep(length);
          }
        });
  }
  void do_write_http_rep(std::size_t length){
    boost::asio::async_write(_browser_socket, boost::asio::buffer(rdata_, length),
        [this](boost::system::error_code ec, std::size_t )
        {
          if (!ec)
          {
            std::cout << "Done passing http reply" << std::endl;
            memset(rdata_, '\0', max_length);
            do_read_http_rep();
          }
        });
  }


  tcp::acceptor _acceptor{io_context};
  tcp::resolver _resolver{io_context};
  tcp::resolver::results_type endpoints;
  tcp::socket _browser_socket{io_context};
  tcp::socket _service_socket{io_context};
  
  enum { max_length = 1024 };
  char rdata_[max_length];
  char wdata_[max_length];
  std::size_t http_req_length;
  std::size_t http_rep_length;
  
  unsigned char input_buffer_[max_length];
  std::vector<unsigned char> output_buffer_;
  struct socks4{
    unsigned char VN;
    unsigned char CD;
    unsigned short DSTPORT; // 2 byte
    unsigned char DSTIP[4]; // 4 byte
    unsigned char nullByte = '\0';
  };
  std::string unresolveDst;
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
  bool isSocks4a(){
    return (req.DSTIP[0] == 0 && req.DSTIP[1] == 0 && req.DSTIP[2] == 0 && req.DSTIP[3] != 0);
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