#include <iostream>
#include <unistd.h>        // this is for gethostname
#include <netdb.h>         // this is for gethostbyname
#include <arpa/inet.h>     // this is for inet_ntoa
#include <cstring>         

using namespace std;

int main() {
    char host[256];

    // to get local host name
    if (gethostname(host, sizeof(host)) == -1) {
        perror("gethostname failed");
        return 1;
    }

    cout << "Host Name: " << host << endl;

    // to get host information
    hostent* he = gethostbyname(host);
    if (he == nullptr) {
        cerr << "gethostbyname failed." << endl;
        return 1;
    }

    // to get and print the first IP address
    in_addr* addr = (in_addr*)he->h_addr_list[0];
    cout << "IP Address: " << inet_ntoa(*addr) << endl;

    return 0;
}