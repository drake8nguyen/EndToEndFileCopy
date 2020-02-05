
// #include "endtoendcheck.h"
#include "c150nastyfile.h"       // for c150nastyfile & framework
#include "c150nastydgmsocket.h"
#include "c150dgmsocket.h"
#include "c150grading.h"
#include "c150debug.h"


#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>               // for errno string formatting
#include <cerrno>
#include <cstring>               // for strerro
#include <iostream>              // for cout
#include <fstream>               // for input files
#include <openssl/sha.h>
#include <cstdlib>
#include <string>
#include <stdio.h>
#include <stdint.h>
#define CHAR_PER_PACKET 256
#define PACKET_QUEUE 70
#define MAX_TRIES 5
using namespace C150NETWORK;
void checkDirectory(char *dirname);
string makeFileName(string dir, string name);
unsigned char *getCheckSum(char *buffer, int buffer_size) {
	unsigned char *obuf = new unsigned char[20];
	SHA1((const unsigned char *)buffer, buffer_size, obuf);	
	return obuf;
}

int main(int argc, char *argv[]) {
    GRADEME(argc, argv);

    //
    // Variable declarations
    //
    ssize_t readlen;              // amount of data read from socket
    void *fopenretval;
    size_t len;
    string errorString;
    char incomingMessage[512];
    char * big_buffer; //for hashing
    struct stat statbuf;  
    size_t sourceSize;
    int fileNastiness, networkNastiness;
    DIR *SRC;                   // Unix descriptor for open directory
    struct dirent *sourceFile;
    unsigned char *checkSum;

    int segment_num = 0;
    char buffer[512];
    int q = 0;
    int seg_ack = 0;
    bool is_timedout, ack_timedout;
    int retries_from_timeout;
    //
    // Make sure command line looks right
    //
    if (argc != 5) {
        fprintf(stderr,"Correct syntxt is: %s <servername> <networknastiness> <filenastiness> <srcdir>\n", argv[0]);
        exit(1);
    }
    networkNastiness = atoi(argv[2]);
    fileNastiness = atoi(argv[3]);   // convert command line string to integer
    checkDirectory(argv[4]);
    SRC = opendir(argv[4]);
    if (SRC == NULL) {
        fprintf(stderr,"Error opening source directory %s\n", argv[4]);     
        exit(8);
    }

    while ((sourceFile = readdir(SRC)) != NULL) {
        // skip the . and .. names
        if ((strcmp(sourceFile->d_name, ".") == 0) || (strcmp(sourceFile->d_name, "..")  == 0 )) 
            continue;          // never copy . or ..
        //        Send / receive / print 
        try {

            // Create the socket
            C150DgmSocket *sock = new C150NastyDgmSocket(networkNastiness);

            // Tell the DGMSocket which server to talk to
            sock -> setServerName(argv[1]);  
            sock -> turnOnTimeouts(1000);
            string filename;
            filename = sourceFile->d_name;

            string sourceName = makeFileName(argv[4], filename);

            if (lstat(sourceName.c_str(), &statbuf) != 0) {
                fprintf(stderr,"copyFile: Error stating supplied source file %s\n", sourceName.c_str());
                exit(20);
            }


            //
            // Make an input buffer large enough for
            // the whole file
            //
            sourceSize = statbuf.st_size;

            NASTYFILE inputFile(fileNastiness); 
            // do an fopen on the input file
            fopenretval = inputFile.fopen(sourceName.c_str(), "rb");  
                                                // wraps Unix fopen
                                                // Note rb gives "read, binary"
                                                // which avoids line end munging

            if (fopenretval == NULL) {
                cerr << "Error opening input file " << sourceName << 
                        " errno=" << strerror(errno) << endl;
                exit(12);
            }
            // Read the whole file, hash, send filename and checkSum
            //
            big_buffer = (char* ) malloc(sourceSize); //big buffer contains the whole file for hashing
            len = inputFile.fread(big_buffer, 1, sourceSize);
            if (len != sourceSize) {
                cerr << "Error reading file " << sourceName << 
                        "  errno=" << strerror(errno) << endl;
                exit(16);
            }
            checkSum = getCheckSum(big_buffer, sourceSize);
            if (inputFile.fseek(0, SEEK_SET)) {
                cerr << "Error resetting file pointer for " << sourceName << 
                        "  errno=" << strerror(errno) << endl;
                exit(16);               
            }
            q = 0;
            segment_num = 0;
            retries_from_timeout = 0;
            *GRADING << "File: " << filename << " , beginning transmission, attempt" << 1 << endl; // or 0 if doesn't support retries
            //NEEDSWORK
            while ((len = inputFile.fread(buffer, 1, CHAR_PER_PACKET)) > 0) {
                seg_ack = 0;
                memcpy(buffer+256, &sourceSize, 4);
                memcpy(buffer+260, &segment_num, 4);
                for (size_t i = 0; i < 20; i++)
                    buffer[264+i] = checkSum[i];
                for (size_t i = 0; i < filename.length(); i++) 
                {
                    buffer[284+i] = filename[i];
                }
                buffer[284+filename.length()] = '\0';
                buffer[511] = '\0';
                sock -> write(buffer, 512);
                readlen = sock->read(incomingMessage, 512);
                ack_timedout = sock -> timedout();
                memcpy(&seg_ack, incomingMessage, 4);
                *GRADING << "File: " << filename << " , finish sending segment" << seg_ack << endl;
                int retries_packet = 0;
                while ((ack_timedout || seg_ack != segment_num) && retries_packet < MAX_TRIES) {
                    ack_timedout = false;
                    sock -> write(buffer, 512);
                    readlen = sock->read(incomingMessage, 512);
                    ack_timedout = sock -> timedout();
                    if (strcmp(incomingMessage, "success") == 0) {
                        goto end;
                    }
                    if (!ack_timedout) {
                        memcpy(&seg_ack, incomingMessage, 4);
                        *GRADING << "File: " << filename << " , finish sending segment" << seg_ack << endl;
                    }
                    retries_packet++;
                }
                if (retries_packet == MAX_TRIES && seg_ack != segment_num) 
                    throw C150NetworkException("Server is not responding 1");
                segment_num++;
                memset(buffer, 0, sizeof(buffer));
                q++;
                if (q == PACKET_QUEUE) {
                    sleep(2);
                    q = 0;
                }
            }

            inputFile.fseek(0,0);
            readlen = sock -> read(incomingMessage, sizeof(incomingMessage));
            is_timedout = sock -> timedout();
            retries_from_timeout = 0;
            *GRADING << "File: " << filename << " , beginning transmission, attempt" << retries_from_timeout + 1 << endl; // or 0 if doesn't support retries
            while (is_timedout && retries_from_timeout < MAX_TRIES) {
                q = 0;
                segment_num = 0;
                seg_ack = 0;
                //NEEDSWORK
                while ((len = inputFile.fread(buffer, 1, CHAR_PER_PACKET)) > 0) {
                    memcpy(buffer+256, &sourceSize, 4);
                    memcpy(buffer+260, &segment_num, 4);
                    for (size_t i = 0; i < 20; i++)
                        buffer[264+i] = checkSum[i];
                    for (size_t i = 0; i < filename.length(); i++)
                        buffer[284+i] = filename[i];
                    buffer[284+filename.length()] = '\0';
                    buffer[511] = '\0';
                    sock -> write(buffer, 512);
                    readlen = sock->read(incomingMessage, 512);
                    ack_timedout = sock -> timedout();
                    memcpy(&seg_ack, incomingMessage, 4);
                    *GRADING << "File: " << filename << " , finish sending segment" << seg_ack << endl;
                    int retries_packet = 0;
                    while ((ack_timedout || seg_ack != segment_num) && retries_packet < MAX_TRIES) {
                        ack_timedout = false;
                        sock -> write(buffer, 512);
                        readlen = sock->read(incomingMessage, 512);
                        ack_timedout = sock -> timedout();
                        if (strcmp(incomingMessage, "success") == 0) {
                            goto end;
                        }
                        if (!ack_timedout) {
                            memcpy(&seg_ack, incomingMessage, 4);
                            *GRADING << "File: " << filename << " , finish sending segment" << seg_ack << endl;
                        }
                        retries_packet++;
                    }
                    if (retries_packet == MAX_TRIES && seg_ack != segment_num) 
                        throw C150NetworkException("Server is not responding 2");
                    segment_num++;
                    memset(buffer, 0, sizeof(buffer));
                    q++;
                    if (q == PACKET_QUEUE) {
                        sleep(2);
                        q = 0;
                    }
                }
                inputFile.fseek(0,0);
                readlen = sock -> read(incomingMessage, sizeof(incomingMessage));
                is_timedout = sock -> timedout();
                retries_from_timeout++;
            }
            if (retries_from_timeout == MAX_TRIES) 
                throw C150NetworkException("Server is not responding 3");

            end:
            (void)readlen;
            int retries_from_failure = 0;
            *GRADING << "File: " << filename << " transmission complete, waiting for end-to-end check, attempt" << retries_from_failure + 1 << endl; // or 0 if doesn't support retries
            while((strcmp(incomingMessage, "success") != 0) && retries_from_failure < MAX_TRIES) {
                cout << "File " << filename << " end-to-end check FAILS -- retrying " << retries_from_failure << endl;
                sock -> write("confirmedFailure", 17);
                retries_from_timeout = 0;
                is_timedout = true;
                while (is_timedout && retries_from_timeout < MAX_TRIES){
                    q = 0;
                    segment_num = 0;
                    seg_ack = 0;
                    retries_from_timeout = 0;
                    //NEEDSWORK
                    while ((len = inputFile.fread(buffer, 1, CHAR_PER_PACKET)) > 0) {
                        memcpy(buffer+256, &sourceSize, 4);
                        memcpy(buffer+260, &segment_num, 4);
                        for (size_t i = 0; i < 20; i++)
                            buffer[264+i] = checkSum[i];
                        for (size_t i = 0; i < filename.length(); i++)
                            buffer[284+i] = filename[i];
                        buffer[284+filename.length()] = '\0';
                        buffer[511] = '\0';
                        sock -> write(buffer, 512);
                        readlen = sock->read(incomingMessage, 512);
                        ack_timedout = sock -> timedout();
                        memcpy(&seg_ack, incomingMessage, 4);
                        *GRADING << "File: " << filename << " , finish sending segment" << seg_ack << endl;
                        int retries_packet = 0;
                        while ((ack_timedout || seg_ack != segment_num) && retries_packet < MAX_TRIES) {
                            ack_timedout = false;
                            sock -> write(buffer, 512);
                            readlen = sock->read(incomingMessage, 512);
                            ack_timedout = sock -> timedout();
                            if (strcmp(incomingMessage, "success") == 0) {
                                goto end;
                            }
                            if (!ack_timedout) {
                                memcpy(&seg_ack, incomingMessage, 4);
                                *GRADING << "File: " << filename << " , finish sending segment" << seg_ack << endl;
                            }
                            retries_packet++;
                        }
                        if (retries_packet == MAX_TRIES && seg_ack != segment_num) 
                            throw C150NetworkException("Server is not responding 4");
                        segment_num++;
                        memset(buffer, 0, sizeof(buffer));
                        q++;
                        if (q == PACKET_QUEUE) {
                            sleep(2);
                            q = 0;
                        }
                    }
                    inputFile.fseek(0,0);
                    readlen = sock -> read(incomingMessage, sizeof(incomingMessage));
                    is_timedout = sock -> timedout();
                    retries_from_timeout++;
                }
                if (retries_from_timeout == MAX_TRIES) 
                    throw C150NetworkException("Server is not responding 5");
                retries_from_failure++;
            }
            if (inputFile.fclose() != 0 ) {
                cerr << "Error closing input file " << sourceName << 
                        " errno=" << strerror(errno) << endl;
                exit(16);
            }
            if (strcmp(incomingMessage, "success") == 0) {
                *GRADING << "File: " << filename << " end-to-end check succeeded, attempt" << retries_from_failure + 1 << endl; 
                cout << "File " << filename << " end-to-end check SUCCEEDS -- informing server" << endl;
                sock -> write("confirmedSuccess", 17);
            }
            else {
                *GRADING << "File: " << filename << " end-to-end check failed, attempt" << retries_from_failure + 1 << endl; 
                cout << "File " << filename << " end-to-end check FAILS -- giving up" << endl;
                sock -> write("confirmedFailure", 17);
                exit(0);
            }

        }
        //  Handle networking errors -- for now, just print message and give up!
        catch (C150NetworkException& e) {
            // Write to debug log
            c150debug->printf(C150ALWAYSLOG,"Caught C150NetworkException: %s\n",
                            e.formattedExplanation().c_str());
            // In case we're logging to a file, write to the console too
            cerr << argv[0] << ": caught C150NetworkException: " 
                << e.formattedExplanation() << endl;
        }
        free(big_buffer);
    }
    closedir(SRC);
    return 0;

}




// ------------------------------------------------------
//
//                   makeFileName
//
// Put together a directory and a file name, making
// sure there's a / in between
//
// ------------------------------------------------------

string makeFileName(string dir, string name) {
  stringstream ss;

  ss << dir;
  // make sure dir name ends in /
  if (dir.substr(dir.length()-1,1) != "/")
    ss << '/';
  ss << name;     // append file name to dir
  return ss.str();  // return dir/name
  
}


void
checkDirectory(char *dirname) {
  struct stat statbuf;  
  if (lstat(dirname, &statbuf) != 0) {
    fprintf(stderr,"Error stating supplied source directory %s\n", dirname);
    exit(8);
  }

  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr,"File %s exists but is not a directory\n", dirname);
    exit(8);
  }
}

