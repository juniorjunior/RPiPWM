#include <iostream>
#include <cstdio>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <cmath>
#include <vector>
#include <typeinfo>

#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>

#include <netinet/in.h>

#define CMD_SETLEVELS   0x01
#define CMD_AUTOPATTERN 0x02
#define CMD_AUTODISABLE 0x03

#define AUTO_DISABLED     0x0000
#define AUTO_CRAZY        0x0001
#define AUTO_CHRISTMAS    0x0002
#define AUTO_HALLOWEEN    0x0003
#define AUTO_JULYFOURTH   0x0004
#define AUTO_THANKSGIVING 0x0005
#define AUTO_EASTER       0x0006
#define AUTO_REMOTE       0x0007

#define NOPARAMETER "NOPARAMETER"

using namespace std;
using namespace std::chrono;


// The GPIO pin numbers used for PWM (not the RPi connector pin numbers)
unsigned int redGPIO = 23;
unsigned int greenGPIO = 24;
unsigned int blueGPIO = 25;

// Initial PWM/color values
double redLevel = 0;
double greenLevel = 0;
double blueLevel = 0;

// Variables for keeping the state of automatic color switching
unsigned int autoMode = 0;
bool autoActive = false;

// Initial delay in milliseconds between each color change while in an auto mode
unsigned int crazyDelay = 250;

// Struct for passing color/PWM value sets
// Colors are obvious. restDuration is how long to rest on this color.
struct colorTriplet {
   double red;
   double green;
   double blue;
   unsigned int restDuration;
};

unsigned int udpMsgCount = 0;
int pbDeviceFd = -1;
unsigned char myTargetID = 0;

// Boolean to indicate if we are in daemon mode or not
bool daemonMode = false;

// Make oldSettings global so we can reset settings on a CTRL-C
struct termios oldSettings;

void resetScreen() {
   char clear[5] = {27, '[', '2', 'J', 0};
   cout << clear;
   cout << "PWM Shifter Running\n";
   cout << "-------------------\n";
   cout << "Red   : " << (unsigned int)(redLevel * 100) << " %\n";
   cout << "Green : " << (unsigned int)(greenLevel * 100) << " %\n";
   cout << "Blue  : " << (unsigned int)(blueLevel * 100) << " %\n";
   cout << "Crazy Speed : " << (crazyDelay/50) << "/20 (restart crazy to apply)\n";
   cout << "autoMode: " << autoMode << "\n";
   cout << "\n";
   cout << "Press 'R' or 'r' to increase/decrease red intensity\n";
   cout << "Press 'G' or 'g' to increase/decrease green intensity\n";
   cout << "Press 'B' or 'b' to increase/decrease blue intensity\n";
   cout << "Press '[' or ']' to increase/decrease all intensity\n";
   cout << "\n";
   cout << "Press 'h' to be scary\n";
   cout << "Press 'e' to summon the easter bunny\n";
   cout << "Press 'x' to get into the holiday spirit\n";
   cout << "Press '4' for an independance celebration\n";
   cout << "Press 'c' to GO CRAZY!!!! (epilepsy warning)\n";
   cout << "Press '-' or '=' to increase/decrease crazy speed\n";
   cout << "\n";
   cout << "Press 'q' to quit\n";
}

// pin is the GPIO number (not the RPi connector pin number)
// level is a value between 0.0 and 1.0
void setColor(unsigned int pin, double level) {
   string cmd = "";

   // Sanity check
   level = abs(level);
   if ( level > 1.0 ) level = 1.0;

   // Create and write the output to the Pi-Blaster device for this color/pin
   cmd = to_string(pin) + "=" + to_string(level) + "\n";
   write(pbDeviceFd, cmd.c_str(), cmd.length());
}

// colors are values between 0.0 and 1.0
void setColors(double red, double green, double blue) {
   string cmd = "";

   // Sanity check
   red = abs(red);
   if ( red > 1.0 ) red = 1.0;
   green = abs(green);
   if ( green > 1.0 ) green = 1.0;
   blue = abs(blue);
   if ( blue > 1.0 ) blue = 1.0;

   // Create and write the output to the Pi-Blaster device for all colors/pins
   cmd = to_string(redGPIO) + "=" + to_string(red) + "\n" + to_string(greenGPIO) + "=" + to_string(green) + "\n" + to_string(blueGPIO) + "=" + to_string(blue) + "\n";
   write(pbDeviceFd, cmd.c_str(), cmd.length());
}

// colors are values between 0.0 and 1.0
// duration is in milliseconds
void rampColors(double red, double green, double blue, double duration) {
   double redInterval, greenInterval, blueInterval;
   unsigned int stepDuration = 5; // Number of milliseconds for each step.

   // The number of steps is the duration divided by the duration of each step.
   unsigned int steps = duration / stepDuration;

   // Calculate the PWM level value for each step on each color independently.
   // This scales the level difference for each step to be linear over the duration.
   redInterval = (red - redLevel)/(double)steps;
   greenInterval = (green - greenLevel)/(double)steps;
   blueInterval = (blue - blueLevel)/(double)steps;

   // Iterate over the steps
   for ( int i = 0; i < steps; i++ ) {
      // Increment each level interval
      redLevel += redInterval;
      greenLevel += greenInterval;
      blueLevel += blueInterval;

      // Set the output color/level and wait for stepDuration milliseconds
      setColors(redLevel, greenLevel, blueLevel);
      this_thread::sleep_for(chrono::milliseconds(stepDuration));
   }
}

// Pass in a vector of colorTriplet structs and an integer for how long to rest between color changes
void autoCycleThread(vector<colorTriplet> colors, unsigned int rampDuration) {
   double redOrig = redLevel;
   double greenOrig = greenLevel;
   double blueOrig = blueLevel;
   double redAuto, greenAuto, blueAuto;
   unsigned int currentIndex = 0;
   double red, green, blue;

   autoActive = true;
   while ( autoMode != AUTO_DISABLED ) {
      red = colors.at(currentIndex).red;
      green = colors.at(currentIndex).green;
      blue = colors.at(currentIndex).blue;

      // If there is only one color triplet and all color values are zero, set the color randomly (i.e. Crazy mode)
      if ( (colors.size() == 1) && (red == 0.0) && (green == 0.0) && (blue == 0.0) ) {
         red = (double)(rand() % 500) / 1000.0;
         green = (double)(rand() % 500) / 1000.0;
         blue = (double)(rand() % 500) / 1000.0;
      }
      rampColors(red, green, blue, rampDuration);
      if ( autoMode != AUTO_DISABLED ) this_thread::sleep_for(chrono::milliseconds(colors.at(currentIndex).restDuration));
      currentIndex++;
      if ( currentIndex == colors.size() ) currentIndex = 0;
   }

   // Set everything back to the "level" values
   rampColors(redOrig, greenOrig, blueOrig, rampDuration);
   autoActive = false;
   return;
}

//
// This function is run as a thread which listens for UDP messages
// The message format is 16 bits for the command and 3x64 bits for
// color values in the order RGB.
void remoteColorThread() {
   int sock, length, bytesReceived;
   socklen_t fromlen;
   struct sockaddr_in server;
   struct sockaddr_in from;
   char buf[8192] = {0};
   unsigned int recvPort = 6565;
   double redUDP, greenUDP, blueUDP;
   unsigned int udpCommand;
   string cmd = "";
   unsigned int rampDuration;
   vector<colorTriplet> colors;
   struct colorTriplet color;
   unsigned char udpTargetID;
   

   // Initialize the listening UDP socket.
   sock = socket(AF_INET, SOCK_DGRAM, 0);
   if ( sock < 0 ) {
       cout << "\nError creating receive socket\n";
       exit(1);
   }
   length = sizeof(server);
   memset(&server, 0, length);
   server.sin_family = AF_INET;
   server.sin_addr.s_addr = INADDR_ANY;
   server.sin_port = htons(recvPort);
   if ( ::bind(sock,(struct sockaddr *)&server,length) < 0 ) {
       cout << "\nError binding to receive port\n";
       exit(1);
   }
   fromlen = sizeof(struct sockaddr_in);

   // Loop forever waiting for UDP messages
   while ( true ) {
      // Clear the input buffer and wait for a message
      memset((char*)buf, 0, 8192);
      bytesReceived = recvfrom(sock, buf, 8192, 0, (struct sockaddr *)&from, &fromlen);
      udpMsgCount++;

      // Get the command from the first 8 bits of the UDP message
      memcpy(&udpCommand, (char*)buf, 1);

      // Get the target ID from the second 8 bits of the UDP message
      memcpy(&udpTargetID, (char*)buf + 1, 1);

      // If the TargetID from the UDP message doesn't match our ID,
      // and isn't set to 0 (zero) (i.e. all targets), then skip out.
      if ( (udpTargetID != myTargetID) && (udpTargetID != 0) ) continue;

      // If we got a CMD_SETLEVELS, do a sanity check on the data and
      // ramp to the new values if we aren't currently in auto mode
      if ( udpCommand == CMD_SETLEVELS ) {
         memcpy(&rampDuration, (char*)buf + 2, 4);
         memcpy(&redUDP, (char*)buf + 6, 8);
         memcpy(&greenUDP, (char*)buf + 14, 8);
         memcpy(&blueUDP, (char*)buf + 22, 8);
         redUDP = abs(redUDP);
         greenUDP = abs(greenUDP);
         blueUDP = abs(blueUDP);
         if ( redUDP > 1.0 ) redUDP = 1.0;
         if ( greenUDP > 1.0 ) greenUDP = 1.0;
         if ( blueUDP > 1.0 ) blueUDP = 1.0;
         if ( autoMode != AUTO_DISABLED ) {
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
         }
         rampColors(redUDP, greenUDP, blueUDP, rampDuration);
      }

      // If we got a CMD_AUTODISABLE then turn off the auto cycler
      if ( udpCommand == CMD_AUTODISABLE ) {
         if ( autoMode != AUTO_DISABLED ) {
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
         }
      }

      // If we got a CMD_AUTOPATTERN then terminate any existing rotation
      // and start a new one from the color sets provided
      if ( udpCommand == CMD_AUTOPATTERN ) {
         unsigned char numTriplets = 0;
         memcpy(&rampDuration, (char*)buf + 2, 4);
         memcpy(&numTriplets, (char*)buf + 6, 1);
         // The input buffer can hold a max of 35 full triplets plus the header so we limit it to that
         if ( numTriplets > 35 ) numTriplets = 35;
         colors.clear();
         // Snag all the colors from the buffer
         for ( unsigned int i = 0; i < numTriplets; i++ ) {
            memcpy(&color, (char*)buf + (7 + (i*28)), 28);
            // Sanity check the incoming color and restDuration data. Zero out color levels which are too high
            color.red = abs(color.red);
            color.green = abs(color.green);
            color.blue = abs(color.blue);
            if ( color.red > 1.0 ) color.red = 0.0;
            if ( color.green > 1.0 ) color.green = 0.0;
            if ( color.blue > 1.0 ) color.blue = 0.0;
            colors.push_back(color);
         }
         // Wait for the current auto cycler to end if it is running
         if ( autoMode != AUTO_DISABLED ) {
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
         }
         autoMode = AUTO_REMOTE;
         thread autoCycleT(autoCycleThread, colors, rampDuration);
         autoCycleT.detach();
      }
   }
}

// This thread monitors the keyboard for manual control of the levels and settings
void keyPressThread() {
   char keyPress = 0;
   struct termios newSettings;
   string cmd = "";
   vector<colorTriplet> autoColors;

   // Set the values to zero on startup
   setColors(0.0, 0.0, 0.0);

   // Store the current stdin settings and change to no-echo/no-return
   tcgetattr(fileno(stdin), &oldSettings);
   newSettings = oldSettings;
   newSettings.c_lflag &= (~ICANON & ~ECHO);
   tcsetattr(fileno(stdin), TCSANOW, &newSettings);

   while ( keyPress != 'q' ) {
      // Set up the keyPress waiting logic. The keyPress loop will use
      // select to wait for the specified interval then continue through
      // the loop. This allows us to update the screen periodically even
      // if there was no keyboard input.
      fd_set set;
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 250000;
      FD_ZERO(&set);
      FD_SET(fileno(stdin), &set);

      // Wait for 250ms then continue on even if no key was pressed
      int res = select(fileno(stdin)+1, &set, NULL, NULL, &tv);
      if ( res > 0 ) {
         read(fileno(stdin), &keyPress, 1);
      } else {
         keyPress = 0;
      }

      // Perform the appropriate actions based on which key was pressed
      if ( (keyPress == 'R') && (redLevel < 1.0) ) {
         redLevel += 0.1;
         setColor(redGPIO, redLevel);
      }
      if ( (keyPress == 'r') && (redLevel > 0.0) ) {
         redLevel -= 0.1;
         setColor(redGPIO, redLevel);
      }
      if ( (keyPress == 'G') && (greenLevel < 1.0) ) {
         greenLevel += 0.1;
         setColor(greenGPIO, greenLevel);
      }
      if ( (keyPress == 'g') && (greenLevel > 0.0) ) {
         greenLevel -= 0.1;
         setColor(greenGPIO, greenLevel);
      }
      if ( (keyPress == 'B') && (blueLevel < 1.0) ) {
         blueLevel += 0.1;
         setColor(blueGPIO, blueLevel);
      }
      if ( (keyPress == 'b') && (blueLevel > 0.0) ) {
         blueLevel -= 0.1;
         setColor(blueGPIO, blueLevel);
      }
      if ( keyPress == '[' ) {
         if ( redLevel < 1.0 ) {
            redLevel += 0.1;
         }
         if ( greenLevel < 1.0 ) {
            greenLevel += 0.1;
         }
         if ( blueLevel < 1.0 ) {
            blueLevel += 0.1;
         }
         setColors(redLevel, greenLevel, blueLevel);
      }
      if ( keyPress == ']' ) {
         if ( redLevel > 0.0 ) {
            redLevel -= 0.1;
         }
         if ( greenLevel > 0.0 ) {
            greenLevel -= 0.1;
         }
         if ( blueLevel > 0.0 ) {
            blueLevel -= 0.1;
         }
         setColors(redLevel, greenLevel, blueLevel);
      }
      if ( keyPress == '-' ) {
         if ( (crazyDelay - 50) >= 50 ) {
            crazyDelay -= 50;
         }
      }
      if ( keyPress == '=' ) {
         if ( (crazyDelay + 50) <= 1000 ) {
            crazyDelay += 50;
         }
      }
      if ( keyPress == 'c' ) {
         if ( autoMode == AUTO_CRAZY ) {
            autoMode = AUTO_DISABLED;
         } else {
            autoMode = AUTO_DISABLED;
            // Wait for any autoCycleThread to finish
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
            autoMode = AUTO_CRAZY;
            autoColors.clear();
            autoColors.push_back({0.0, 0.0, 0.0, 0}); // only one element and all zero colors means set them randomly
            thread autoCycleT(autoCycleThread, autoColors, crazyDelay);
            autoCycleT.detach();
         }
      }
      if ( keyPress == 'x' ) {
         if ( autoMode == AUTO_CHRISTMAS ) {
            autoMode = AUTO_DISABLED;
         } else {
            autoMode = AUTO_DISABLED;
            // Wait for any autoCycleThread to finish
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
            autoMode = AUTO_CHRISTMAS;
            autoColors.clear();
            autoColors.push_back({1.0, 0.0, 0.0, 2000}); // red
            autoColors.push_back({0.0, 1.0, 0.0, 2000}); // green
            thread autoCycleT(autoCycleThread, autoColors, 1000);
            autoCycleT.detach();
         }
      }
      if ( keyPress == '4' ) {
         if ( autoMode == AUTO_JULYFOURTH ) {
            autoMode = AUTO_DISABLED;
         } else {
            autoMode = AUTO_DISABLED;
            // Wait for any autoCycleThread to finish
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
            autoMode = AUTO_JULYFOURTH;
            autoColors.clear();
            autoColors.push_back({1.0, 0.0, 0.0, 1000}); // red
            autoColors.push_back({0.5, 0.5, 0.5, 1000}); // white
            autoColors.push_back({0.0, 0.0, 1.0, 1000}); // blue
            thread autoCycleT(autoCycleThread, autoColors, 1000);
            autoCycleT.detach();
         }
      }
      if ( keyPress == 'e' ) {
         if ( autoMode == AUTO_EASTER ) {
            autoMode = AUTO_DISABLED;
         } else {
            autoMode = AUTO_DISABLED;
            // Wait for any autoCycleThread to finish
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
            autoMode = AUTO_EASTER;
            autoColors.clear();
            autoColors.push_back({1.0, 0.012, 0.753, 1000}); // pink
            autoColors.push_back({0.031, 1.0, 0.969, 1000}); // cyan
            autoColors.push_back({1.0, 0.988, 0.02, 1000}); // yellow
            thread autoCycleT(autoCycleThread, autoColors, 1000);
            autoCycleT.detach();
         }
      }
      if ( keyPress == 'h' ) {
         if ( autoMode == AUTO_HALLOWEEN ) {
            autoMode = AUTO_DISABLED;
         } else {
            autoMode = AUTO_DISABLED;
            // Wait for any autoCycleThread to finish
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
            autoMode = AUTO_HALLOWEEN;
            autoColors.clear();
            autoColors.push_back({1.0, 0.094, 0.0, 1000}); // orange
            autoColors.push_back({0.0, 0.0, 0.0, 250}); // black
            thread autoCycleT(autoCycleThread, autoColors, 1000);
            autoCycleT.detach();
         }
      }
      if ( keyPress == 'q' ) {
         if ( autoMode != AUTO_DISABLED ) {
            cout << "\nWaiting for auto cyclers to stop...";
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(10));
            }
            cout << "\n\n";
         }
      }
      resetScreen();
   }

   // Set all colors to zero and close the Pi-Blaster device
   setColors(0.0, 0.0, 0.0);

   // Restore to original settings
   tcsetattr(fileno(stdin), TCSANOW, &oldSettings);

   return;
}

string getParameter(string needle, const int argc, const char* argv[]) {
   string curP;
   string value;

   if ( argc == 1 ) return NOPARAMETER;
   for ( int i = 1; i < argc; i++ ) {
      curP = string(argv[i]);
      if ( curP.empty() ) continue;
      if ( curP.length() < needle.length() ) continue;
      if ( curP.compare(0, needle.length(), needle) == 0 ) {
         int p = curP.find("=", 0);
         if ( (p != string::npos) && ((p + 1) <= curP.length()) ) {
            value = curP.substr(p + 1);
         } else {
            value = "";
         }
         return value;
      }
   }
   return NOPARAMETER;
}

// Catch CTRL-C and set pins to zero
void sigHandler(int s) {
   if ( !daemonMode ) {
      // Restore to original settings
      tcsetattr(fileno(stdin), TCSANOW, &oldSettings);
   }
   // Set all colors to zero and close the Pi-Blaster device
   setColors(0.0, 0.0, 0.0);
   close(pbDeviceFd);
   exit(1);
}

//
// Valid command line parameters:
//    --test   : This makes the output bind to /dev/null instead of the pi-blaster device for testing
//    --daemon : This makes the application run in daemon mode (i.e. no keypress monitoring and no screen output)
//
int main (int argc, const char* argv[], char* envp[]) {
   string deviceName = "";
   string pValue;

   signal (SIGINT, sigHandler);

   pValue = getParameter("--help", argc, argv);
   if ( pValue != NOPARAMETER ) {
      cout << "\nUsage: pwmdemo [options]\n";
      cout << "   Options:\n";
      cout << "      --help : This help\n";
      cout << "      --test : Use /dev/null instead of /dev/pi-blaster (for testing)\n";
      cout << "      --daemon : Don't output to the screen or start the keyPress thread\n\n";
      return 0;
   }

   pValue = getParameter("--test", argc, argv);
   if ( pValue != NOPARAMETER ) {
      deviceName = "/dev/null";
   } else {
      deviceName = "/dev/pi-blaster";
   }

   pValue = getParameter("--daemon", argc, argv);
   if ( pValue != NOPARAMETER ) {
      daemonMode = true;
   }

   pValue = getParameter("--id", argc, argv);
   if ( pValue != NOPARAMETER ) {
      if ( !pValue.empty() ) myTargetID = (unsigned char)stoi(pValue);
   }

   // Open the Pi-Blaster device for writing
   pbDeviceFd = open(deviceName.c_str(), O_WRONLY | O_NONBLOCK);
   // Fail out if Pi-Blaster PWM daemon is not available
   if ( pbDeviceFd == -1 ) {
      cout << "\nERROR: Could not open " << deviceName << "\n\n";
      return 1;
   }

   // If we are in daemon mode, don't start the keypress thread or write to the screen
   // Always start the remoteColor thread, but detach it if not in daemon mode
   if ( daemonMode ) {
      thread remoteColorT(remoteColorThread);
      remoteColorT.join();
   } else {
      resetScreen();
      thread keyPressT(keyPressThread);
      thread remoteColorT(remoteColorThread);
      remoteColorT.detach();
      keyPressT.join();
   }

   close(pbDeviceFd);
   return 0;
}
