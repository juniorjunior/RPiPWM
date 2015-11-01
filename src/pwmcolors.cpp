#include <iostream>
#include <cstdio>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <cmath>
#include <vector>
#include <typeinfo>
#include <bitset>

#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <ncurses.h>

#include <sys/socket.h>

#include <netinet/in.h>

#define CMD_OFF         0x00
#define CMD_SETLEVELS   0x01
#define CMD_AUTOPATTERN 0x02
#define CMD_AUTODISABLE 0x03

#define AUTO_DISABLED   0x00
#define AUTO_ACTIVE     0x01

#define NOPARAMETER "NOPARAMETER"

// The GPIO pin numbers used for PWM (not the RPi connector pin numbers)
#define GPIO_RED   23
#define GPIO_GREEN 24
#define GPIO_BLUE  25

using namespace std;
using namespace std::chrono;

// Initial PWM/color values
double redLevel = 0;
double greenLevel = 0;
double blueLevel = 0;
double redStatic = 0;
double greenStatic = 0;
double blueStatic = 0;

// Variables for keeping the state of automatic color switching
unsigned int autoMode = 0;
bool autoActive = false;
bool newCommand = false;

// Initial delay in milliseconds between each color change while in an auto mode
unsigned int crazyDelay = 250;

// Struct for storing color/PWM value sets
// Colors are obvious. restDuration is how long to rest on this color.
struct colorTriplet {
   double red;
   double green;
   double blue;
   unsigned int restDuration;
};

unsigned int udpMsgCount = 0;
int pbDeviceFd = -1;
unsigned int myTargetID = 0;

// Boolean to indicate if we are in daemon mode or not
bool daemonMode = false;

// Make oldSettings global so we can reset settings on a CTRL-C
struct termios oldSettings;

void cleanExit(int level) {
   tcsetattr(fileno(stdin), TCSANOW, &oldSettings);
   exit(level);
}

void resetScreen() {
   char clear[5] = {27, '[', '2', 'J', 0};
   cout << clear;
   cout << "PWM Shifter Running\n";
   cout << "-------------------\n";
   cout << "Red   : " << (unsigned int)(redLevel * 100) << " (" << (unsigned int)(redStatic * 100) << ") %\n";
   cout << "Green : " << (unsigned int)(greenLevel * 100) << " (" << (unsigned int)(greenStatic * 100) << ") %\n";
   cout << "Blue  : " << (unsigned int)(blueLevel * 100) << " (" << (unsigned int)(blueStatic * 100) << ") %\n";
   cout << "Crazy Speed : " << (crazyDelay/50) << "/20 (restart crazy to apply)\n";
   cout << "autoMode: " << autoMode << "\n";
   cout << "autoActive: " << ((autoActive) ? "True" : "False") << "\n";
   cout << "ID: " << myTargetID << "\n";
   cout << "UDP Messages: " << udpMsgCount << "\n";
   cout << "\n";
   cout << "Press 'R' or 'r' to increase/decrease static red intensity\n";
   cout << "Press 'G' or 'g' to increase/decrease static green intensity\n";
   cout << "Press 'B' or 'b' to increase/decrease static blue intensity\n";
   cout << "Press '[' or ']' to increase/decrease all static intensity\n";
   cout << "\n";
   cout << "Press 'h' to be scary\n";
   cout << "Press 'e' to summon the easter bunny\n";
   cout << "Press 'x' to get into the holiday spirit\n";
   cout << "Press '4' for an independance celebration\n";
   cout << "Press 'c' to GO CRAZY!!!! (epilepsy warning)\n";
   cout << "Press '-' or '=' to increase/decrease crazy speed\n";
   cout << "Press '.' to disable any auto-cycler\n";
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

   // Set the appropriate global color level
   if ( pin == GPIO_RED ) redLevel = level;
   if ( pin == GPIO_GREEN ) greenLevel = level;
   if ( pin == GPIO_BLUE ) blueLevel = level;

   // Create and write the output to the Pi-Blaster device for this color/pin
   cmd = to_string(pin) + "=" + to_string(level) + "\n";
   write(pbDeviceFd, cmd.c_str(), cmd.length());
}

// This function sleeps a thread for a specified duration.
// However, it breaks that duration up into 5 millisecond
// intervals so it can abort the full duration if a new
// command has been received.
void gentleSleep(unsigned int duration) {
   unsigned int timeRemaining = duration;
   unsigned int interval;

   while ( !newCommand && (timeRemaining > 0) ) {
      if ( timeRemaining > 5 ) {
         interval = 5;
         timeRemaining -= 5;
      } else {
         interval = timeRemaining;
         timeRemaining = 0;
      }
      this_thread::sleep_for(chrono::milliseconds(interval));
   }
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

   // Set the color level variables to the new levels
   redLevel = red;
   greenLevel = green;
   blueLevel = blue;

   // Create and write the output to the Pi-Blaster device for all colors/pins
   cmd = to_string(GPIO_RED) + "=" + to_string(red) + "\n" + to_string(GPIO_GREEN) + "=" + to_string(green) + "\n" + to_string(GPIO_BLUE) + "=" + to_string(blue) + "\n";
   write(pbDeviceFd, cmd.c_str(), cmd.length());
}

// colors are values between 0.0 and 1.0
// duration is in milliseconds
void rampColors(double red, double green, double blue, unsigned int duration) {
   double redInterval, greenInterval, blueInterval;
   double redNew, greenNew, blueNew;
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
      redNew = redLevel + redInterval;
      greenNew = greenLevel + greenInterval;
      blueNew = blueLevel + blueInterval;

      // Set the output color/level and wait for stepDuration milliseconds
      setColors(redNew, greenNew, blueNew);
      gentleSleep(stepDuration);
      if ( newCommand ) break;
   }
}

// Pass in a vector of colorTriplet structs and an integer for how long to rest between color changes
void autoCycleThread(vector<colorTriplet> colors, unsigned int rampDuration) {
   double red, green, blue;
   unsigned int currentIndex = 0;

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
      gentleSleep(colors.at(currentIndex).restDuration);
      currentIndex++;
      if ( currentIndex == colors.size() ) currentIndex = 0;
   }

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
   char buf[1024] = {0};
   unsigned int recvPort = 6565;
   unsigned char redUDP, greenUDP, blueUDP;
   unsigned int restDurationUDP;
   unsigned char udpCommand;
   string cmd = "";
   unsigned int rampDuration;
   vector<colorTriplet> colors;
   struct colorTriplet color;
   bool udpTarget = false;
   unsigned long long targetBitField;
   

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
      memset((char*)buf, 0, 1024);
      bytesReceived = recvfrom(sock, buf, 1024, 0, (struct sockaddr *)&from, &fromlen);

      // Get the command from the first 8 bits of the UDP message
      memcpy(&udpCommand, (char*)buf, 1);

      // Get the target ID bitfield from bytes 2-9 of the UDP message
      memcpy(&targetBitField, (char*)buf + 1, 8);

      // The incoming target IDs are in a 64bit bitfield, one bit per ID
      // which provides 64 possible unique IDs and all zeros to indicate
      // the message is intended for all targets. If myTargetID isn't set
      // in the bitfield, and the bitfield isn't zero, skip this message.
      if ( (myTargetID != 0) && (targetBitField != 0) && (((unsigned long long)pow(2, (myTargetID - 1)) & targetBitField) != (unsigned long long)pow(2, myTargetID - 1)) ) continue;

      // Only count messages intended for us
      udpMsgCount++;

      // If we got a CMD_SETLEVELS, do a sanity check on the data and
      // ramp to the new values if we aren't currently in auto mode
      if ( udpCommand == CMD_SETLEVELS ) {
         newCommand = true;
         memcpy(&rampDuration, (char*)buf + 9, 4);
         memcpy(&redUDP, (char*)buf + 13, 1);
         memcpy(&greenUDP, (char*)buf + 14, 1);
         memcpy(&blueUDP, (char*)buf + 15, 1);
         if ( autoMode != AUTO_DISABLED ) {
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(5));
            }
         }
         redStatic = (double)(redUDP/255.0);
         greenStatic = (double)(greenUDP/255.0);
         blueStatic = (double)(blueUDP/255.0);
         newCommand = false;
         rampColors(redStatic, greenStatic, blueStatic, rampDuration);
      }

      // If we got a CMD_OFF then turn off the auto cycler (if active) and set colors to 0 (zero)
      if ( udpCommand == CMD_OFF ) {
         newCommand = true;
         if ( autoMode != AUTO_DISABLED ) {
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(5));
            }
         }
         redStatic = 0.0;
         greenStatic = 0.0;
         blueStatic = 0.0;
         newCommand = false;
         setColors(0.0, 0.0, 0.0);
      }

      // If we got a CMD_AUTODISABLE then turn off the auto cycler
      if ( udpCommand == CMD_AUTODISABLE ) {
         newCommand = true;
         if ( autoMode != AUTO_DISABLED ) {
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(5));
            }
         }
         newCommand = false;
         // Set everything back to the "static" values
         rampColors(redStatic, greenStatic, blueStatic, 1000);
      }

      // If we got a CMD_AUTOPATTERN then terminate any existing rotation
      // and start a new one from the color sets provided
      if ( udpCommand == CMD_AUTOPATTERN ) {
         newCommand = true;
         unsigned char numTriplets = 0;
         memcpy(&rampDuration, (char*)buf + 9, 4);
         memcpy(&numTriplets, (char*)buf + 13, 1);
         // The input buffer can hold a max of 35 full triplets plus the header so we limit it to that
         if ( numTriplets > 35 ) numTriplets = 35;
         colors.clear();
         // Snag all the colors from the buffer
         for ( unsigned int i = 0; i < numTriplets; i++ ) {
            memcpy(&redUDP, (char*)buf + (14 + (i*7)), 1);
            memcpy(&greenUDP, (char*)buf + (15 + (i*7)), 1);
            memcpy(&blueUDP, (char*)buf + (16 + (i*7)), 1);
            memcpy(&restDurationUDP, (char*)buf + (17 + (i*7)), 4);
            color.red = (double)redUDP/255.0;
            color.green = (double)greenUDP/255.0;
            color.blue = (double)blueUDP/255.0;
            color.restDuration = restDurationUDP;
            // Sanity check the incoming color and restDuration data. Zero out color levels which are too high
            colors.push_back(color);
         }
         // Wait for the current auto cycler to end if it is running
         if ( autoMode != AUTO_DISABLED ) {
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(5));
            }
         }
         autoMode = AUTO_ACTIVE;
         newCommand = false;
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
      if ( (keyPress == 'R') && (redStatic < 1.0) ) {
         redStatic += 0.1;
         if ( redStatic > 1.0 ) redStatic = 1.0;
         if ( autoMode == AUTO_DISABLED ) setColor(GPIO_RED, redStatic);
      }
      if ( (keyPress == 'r') && (redStatic > 0.0) ) {
         redStatic -= 0.1;
         if ( redStatic < 0.0 ) redStatic = 0.0;
         if ( autoMode == AUTO_DISABLED ) setColor(GPIO_RED, redStatic);
      }
      if ( (keyPress == 'G') && (greenStatic < 1.0) ) {
         greenStatic += 0.1;
         if ( greenStatic > 1.0 ) greenStatic = 1.0;
         if ( autoMode == AUTO_DISABLED ) setColor(GPIO_GREEN, greenStatic);
      }
      if ( (keyPress == 'g') && (greenStatic > 0.0) ) {
         greenStatic -= 0.1;
         if ( greenStatic < 0.0 ) greenStatic = 0.0;
         if ( autoMode == AUTO_DISABLED ) setColor(GPIO_GREEN, greenStatic);
      }
      if ( (keyPress == 'B') && (blueStatic < 1.0) ) {
         blueStatic += 0.1;
         if ( blueStatic > 1.0 ) blueStatic = 1.0;
         if ( autoMode == AUTO_DISABLED ) setColor(GPIO_BLUE, blueStatic);
      }
      if ( (keyPress == 'b') && (blueStatic > 0.0) ) {
         blueStatic -= 0.1;
         if ( blueStatic < 0.0 ) blueStatic = 0.0;
         if ( autoMode == AUTO_DISABLED ) setColor(GPIO_BLUE, blueStatic);
      }
      if ( keyPress == '[' ) {
         if ( redStatic < 1.0 ) {
            redStatic += 0.1;
         }
         if ( greenStatic < 1.0 ) {
            greenStatic += 0.1;
         }
         if ( blueStatic < 1.0 ) {
            blueStatic += 0.1;
         }
         if ( redStatic > 1.0 ) redStatic = 1.0;
         if ( greenStatic > 1.0 ) greenStatic = 1.0;
         if ( blueStatic > 1.0 ) blueStatic = 1.0;
         if ( autoMode == AUTO_DISABLED ) setColors(redStatic, greenStatic, blueStatic);
      }
      if ( keyPress == ']' ) {
         if ( redStatic > 0.0 ) {
            redStatic -= 0.1;
         }
         if ( greenStatic > 0.0 ) {
            greenStatic -= 0.1;
         }
         if ( blueStatic > 0.0 ) {
            blueStatic -= 0.1;
         }
         if ( redStatic < 0.0 ) redStatic = 0.0;
         if ( greenStatic < 0.0 ) greenStatic = 0.0;
         if ( blueStatic < 0.0 ) blueStatic = 0.0;
         if ( autoMode == AUTO_DISABLED ) setColors(redStatic, greenStatic, blueStatic);
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
         newCommand = true;
         autoMode = AUTO_DISABLED;
         // Wait for any autoCycleThread to finish
         while ( autoActive ) {
            this_thread::sleep_for(chrono::milliseconds(5));
         }
         newCommand = false;
         autoMode = AUTO_ACTIVE;
         autoColors.clear();
         autoColors.push_back({0.0, 0.0, 0.0, 0}); // only one element and all zero colors means set them randomly
         thread autoCycleT(autoCycleThread, autoColors, crazyDelay);
         autoCycleT.detach();
      }
      if ( keyPress == 'x' ) {
         newCommand = true;
         autoMode = AUTO_DISABLED;
         // Wait for any autoCycleThread to finish
         while ( autoActive ) {
            this_thread::sleep_for(chrono::milliseconds(5));
         }
         newCommand = false;
         autoMode = AUTO_ACTIVE;
         autoColors.clear();
         autoColors.push_back({1.0, 0.0, 0.0, 2000}); // red
         autoColors.push_back({0.0, 1.0, 0.0, 2000}); // green
         thread autoCycleT(autoCycleThread, autoColors, 1000);
         autoCycleT.detach();
      }
      if ( keyPress == '4' ) {
         newCommand = true;
         autoMode = AUTO_DISABLED;
         // Wait for any autoCycleThread to finish
         while ( autoActive ) {
            this_thread::sleep_for(chrono::milliseconds(5));
         }
         newCommand = false;
         autoMode = AUTO_ACTIVE;
         autoColors.clear();
         autoColors.push_back({1.0, 0.0, 0.0, 1000}); // red
         autoColors.push_back({0.5, 0.5, 0.5, 1000}); // white
         autoColors.push_back({0.0, 0.0, 1.0, 1000}); // blue
         thread autoCycleT(autoCycleThread, autoColors, 1000);
         autoCycleT.detach();
      }
      if ( keyPress == 'e' ) {
         newCommand = true;
         autoMode = AUTO_DISABLED;
         // Wait for any autoCycleThread to finish
         while ( autoActive ) {
            this_thread::sleep_for(chrono::milliseconds(5));
         }
         newCommand = false;
         autoMode = AUTO_ACTIVE;
         autoColors.clear();
         autoColors.push_back({1.0, 0.012, 0.753, 1000}); // pink
         autoColors.push_back({0.031, 1.0, 0.969, 1000}); // cyan
         autoColors.push_back({1.0, 0.988, 0.02, 1000}); // yellow
         thread autoCycleT(autoCycleThread, autoColors, 1000);
         autoCycleT.detach();
      }
      if ( keyPress == 'h' ) {
         newCommand = true;
         autoMode = AUTO_DISABLED;
         // Wait for any autoCycleThread to finish
         while ( autoActive ) {
            this_thread::sleep_for(chrono::milliseconds(5));
         }
         newCommand = false;
         autoMode = AUTO_ACTIVE;
         autoColors.clear();
         autoColors.push_back({1.0, 0.094, 0.0, 1000}); // orange
         autoColors.push_back({0.0, 0.0, 0.0, 250}); // black
         thread autoCycleT(autoCycleThread, autoColors, 1000);
         autoCycleT.detach();
      }
      if ( keyPress == '.' ) {
         if ( autoMode != AUTO_DISABLED ) {
            newCommand = true;
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(5));
            }
            newCommand = false;
            // Set everything back to the "static" values
            rampColors(redStatic, greenStatic, blueStatic, 1000);
         }
      }
      if ( keyPress == 'q' ) {
         newCommand = true;
         if ( autoMode != AUTO_DISABLED ) {
            cout << "\nWaiting for auto cyclers to stop...";
            autoMode = AUTO_DISABLED;
            while ( autoActive ) {
               this_thread::sleep_for(chrono::milliseconds(5));
            }
            cout << "\n\n";
         }
         newCommand = false;
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
      cout << "      --id   : The ID for this daemon. Valid from 0 to 64. Defaults to 0.\n";
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
      if ( !pValue.empty() ) myTargetID = (unsigned int)stoi(pValue);
   }
   if ( myTargetID > 64 ) {
      cout << "\nERROR: ID must be between 0 and 64\n\n";
      return 1;
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
