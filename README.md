# DAG Blockchain
This project is focused on creating and implementing a Blockchain protocol based on the Directed Acyclic Graph stucture. Idealy the result would be to achieve better scalabilty and transaction per minute ratio than a typical blockchain, by constructing and verifing multiple transaction blocks concurrently.

**_This is still an ongoing project_**

The paper that the core foundation is based on:  
[Link](https://aptoslabs.com/pdf/2102.08325.pdf)     

## Code structure
* `DataStructs` folder: A thread safe implamentation of some standard c++ classes used on the project.
  
* `Server.h`: The server-side of every peer. It is basically a multiplexing tcp server with some more enchanced utilities. A matcing client exists in `Client.h`.
  
* `Networking.h`: Here we have the most of the networking setup and communication functionalities. 
  
* `vertex.h`: The core of our protocol. A unit of structured information about the transfer block that a validator proposes at every round.
  
* `Peer.h`: The final entity of a validator and its functions. There we can see the majority of the protocol's actions.



## Compilation and Testing
In case you want to give it a try:  
* Create a text file with the Peer IP addresses and the port they should be listening for incoming connections. Example:
```
192.168.0.2 3297
192.168.0.3 3764 
192.168.0.7 4999
127.0.0.1 4533
...
```
Once the file is ready, type the following to compile and run the simulation:
```sh
g++ main.cpp vertex.cpp networking.cpp peer.cpp -o main.exe -lcryptopp
./main.exe <file.txt> <peers quantity> <termination round>
```

### System requirments
* You need to have installed the CryptoPP, an open-source C++ class library of cryptographic schemes.     
You can find it [here](https://github.com/weidai11/cryptopp).
* The implementation is doing quite heavy thread usage. If your CPU hardware is limited, it would go smoother if you keep the **peers quantity** argument into a reasonably low number [10-40], depending on your system. 
