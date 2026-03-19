# **Apple Music ESP32 Display By Marlou Vincent Ruiz**

#### **A small display based on an ESP32 c3 super mini that shows the current song playing on desktop Apple Music**

![image alt]([https://github.com/AEzirrr/Apple-Music-ESP32-Display/blob/4f53cd4df6e98eee22222770171ee1dfaffafb8b/image_2026-03-20_072319719.png](https://github.com/AEzirrr/Apple-Music-ESP32-Display/blob/6ec90a064f33753bb449e6c9c229f032fa01df67/Apple%20Music%20ESP32%20Display%20Screenshot.jpg))

This mini project started when i saw videos of other developers and hobbyist modifying the old and out of service Spotify car thing device. I initially wanted to try making one myself since i am fond of desk accessories but in the PH, the device was pretty rare. So i decided to make a somewhat similar version using a very affordable ESP32 c3 super mini microcontroller and ST7789 1.9 inch display. I have some experience in making Arduino based projects in the past but I am not very proficient in it. With the help of AI I was able to accelerate this project's development and learn different things like setting up a local server using python that gets information from apple music in the background and how to utilize the ESP32's wifi capabilities.



##### **Features**

**Live Song Info:** Displays Track Title, Artist, and real-time playback duration.



**Album Art:** Downloads and renders the current album art directly to the screen.



**Progress Bar:** A smooth, live-updating track progress bar.



**Loading Animation:** A custom spinner animation when the player is paused or idle.



**Auto-Reconnect:** Scans a provided list of your desktop's possible local IP addresses to automatically find and connect to the active server.



##### **Hardware Used**

**Microcontroller:** ESP32-C3 Super Mini

**Display:** 1.9" ST7789 IPS TFT Display (SPI)





##### **Apple Music Info Collector**



The nowPlaying.py script runs quietly in the background, grabs your currently playing Apple Music track, and presents that info to a local web address so other apps or devices (in our case, our ESP32 c3) can use it.



###### **Here is what it does:**



* It hooks directly into Windows media controls to pull the track title, artist, playback state, and album art from Apple Music.



* It uses Quart to format the data and Hypercorn to serve it locally. You can access the live data at http://localhost:5000/nowplaying.



* Instead of spamming Apple Music for updates every millisecond, it calculates the live timestamp on its own to keep the PC running smoothly. (prevents it from having high memory usage which I encountered in the past)



* It lives in your Windows system tray. Just right-click the little square icon to quit the server if needed.



###### **How to set it up:**



* Simply run the the now playing batch file or python file (do keep in mind the libraries needed)





#### **ESP32 c3 sketch (sketch\_sep4a)**



the sketch contains all the necessary code for getting the information from the local web address and process it in the ESP32 to display in on screen



###### **How to set it up:**

* Open the sketch and enter your network details
* Update the server IP list of potential addresses for your pc ex.(http://192.168.1.5:5000/nowplaying, http://192.168.1.6:5000/nowplaying)
* Upload the sketch into your ESP32 (again, do note the libraries needed)



#### **ESP32 and ST7789 Pin Guide**



|ST7789 Pin|ESP32-C3 Pin|Notes|
|-|-|-|
|GND|GND|Ground|
|VCC|3.3v|Power|
|SCL/SCK|6|SPI Clock|
|SDA/MOSI|7|SPI Data|
|RES|1|Reset|
|DC|2|Data/Command|
|CS|10|Chip Select|
|BLK|0|Backlight|



Enjoy! :)



