
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <Update.h>


#define PWM_PIN     25       // Definicja pinu GPIO, do którego podłączona jest dioda LED
#define BUTTON_PIN  12       // Definicja pinu GPIO, do którego podłączony jest przycisk

// Definicje stanów programu
#define STATE_STARTING                  0
#define STATE_CONNECTING_TO_WIFI        1
#define STATE_RUNNING                   2

// Struktura danych do przechowywania ustawień
struct Settings {
    char ledName[32] = "";      // Nazwa urządzenia
    bool ledEnabled;            // Stan diody LED
    uint8_t ledBrightness;      // Jasność diody LED
    bool staticIP;              // Czy używać stałego adresu IP
    IPAddress myIP;             // Adres IP
    IPAddress mySubnetMask;     // Maska podsieci
    IPAddress myGateway;        // Brama sieciowa
};

const int ledChannel =  0;      // Kanał biblioteki LEDC
const int freq =        5000;   // Częstotliwość PWM
const int resolution =  8;      // Rozdzielczość PWM (8-bit = 0-255)
WebServer server(80);           // Inicjalizacja serwera HTTP na porcie 80
uint8_t state = STATE_STARTING; // Definicja zmiennej przechowującej stan programu i zainicjalizowanie jej wartością początkową

Settings settings;              // Zmienna przechowująca ustawienia   
bool firstRun = true;           // Flaga informująca o pierwszym uruchomieniu programu

unsigned long lastDebounceTime = 0; // Czas ostatniego odczytu przycisku
unsigned long debounceDelay = 50;   // Opóźnienie anty-drganiowe przycisku
bool lastButtonState = LOW;         // Stan przycisku z poprzedniego odczytu
bool buttonPressed = false;         // Flaga informująca o wciśnięciu przycisku


// Funkcja zwracająca nagłówek HTML
// Funkcja przyjmuje jeden argument typu bool, który określa, czy strona ma być przekierowana do strony głównej
String htmlHead(bool redirectToMain) {
    String head ="<head>"
    "<meta charset=\"ASCII\">"
    "<meta name=\"viewport\"content=\"width=device-width,initial-scale=1.0\">"
    "<title>LED Dimmer setup</title>"
    "<style>"
        "a{color:#0F79E0}"
        "body {"
            "font-family: sans-serif;"
            "background-color: #f4f4f4;"
            "margin: 60px;"
            "display: flex;"
            "flex-direction: column;"
            "align-items: center;"
        "}"
        ".container {"
            "background-color: white;"
            "border-radius: 5px;"
            "box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);"
            "padding: 20px;"
            "margin-bottom: 20px;"
            "width: 600px;"
            "max-width: 90%;"
        "}"
        ".logocontainer {"
            "background-color: white;"
            "border-radius: 5px;"
            "box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);"
            "padding: 10px;"
            "margin-bottom: 20px;"
            "max-width: 600px;"
        "}"
        ".logoimg {"
            "max-width: 600px;"
        "}"
        "h1, h2 {"
            "color: #333;"
            "margin-bottom: 10px;"
        "}"
        "label {"
            "display: inline;"
            "margin-bottom: 5px;"
            "font-weight: bold;"
        "}"
        "input[type=\"text\"],"
        "input[type=\"password\"],"
        "input[type=\"checkbox\"],"
        "input[type=\"file\"],"
        "input[type=\"number\"],"
        "select {"
            "width: 100%;"
            "padding: 8px;"
            "margin-bottom: 15px;"
            "box-sizing: border-box;"
        "}"
        "input[type=\"checkbox\"] {"
            "width: auto;"
            "margin-right: 10px;"
        "}"
        ".ip-fields {"
            "display: flex;"
            "justify-content: space-between;"
        "}"
        ".ip-fields input {"
            "width: calc(23% - 10px);"
            "margin-right: 5px;"
        "}"
        "input[type=\"submit\"],"
        "button {"
            "padding: 10px 20px;"
            "background-color: #28a745;"
            "color: white;"
            "border: none;"
            "border-radius: 5px;"
            "cursor: pointer;"
            "transition: background-color 0.3s ease;"
            "margin: 5px 5px 5px"
        "}"
        "input[type=\"submit\"]:hover,"
        "button:hover {"
            "background-color: #218838;"
        "}"
        ".status p {"
            "margin-bottom: 5px;"
        "}"
        "footer {"
            "position: fixed;"
            "bottom: 0;"
            "left: 0;"
            "width: 100%;"
            "background-color: #333;"
            "color: white;"
            "text-align: center;"
        "}"
    "</style>";

    if (redirectToMain) {
        head += "<meta http-equiv=\"refresh\" content=\"5;url=/\" /></head>";
    } else {
        head += "</head>";
    }

    return head;
}

// Zmienna przechowująca stopkę HTML
String htmlFooter = "<footer>&nbsp;&copy; 2025 Piotr Szpunar<br></footer>";

// Funkcja zwracająca status połączenia z siecią WiFi
String getConnectionStatusString() {
    switch (WiFi.status()) {
        case WL_CONNECTED:
            return "Connected to network";
        case WL_NO_SSID_AVAIL:
            return "Network not found";
        case WL_CONNECT_FAILED:
            return "Invalid password";
        case WL_IDLE_STATUS:
            return "Changing state...";
        case WL_DISCONNECTED:
            return "Station mode disabled";
        default:
            return "Timeout";
    }
}

// Funkcja zwracająca nazwę sieci WiFi
String getSSID() {
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  return String(reinterpret_cast<const char *>(conf.sta.ssid));
}

// Funkcja obsługująca główną stronę
void handleRoot() {
    server.send(200, "text/html", "<!DOCTYPE html><html>"
        + htmlHead(false) +
        "<script>"
            "function switchIpField(e){"
                "console.log(\"switch\");"
                "console.log(e);"
                "var target=e.srcElement||e.target;"
                "var maxLength=parseInt(target.attributes[\"maxlength\"].value,10);"
                "var myLength=target.value.length;"
                "if(myLength>=maxLength){"
                    "var next=target.nextElementSibling;"
                    "if(next!=null){"
                        "if(next.className.includes(\"IP\")){"
                            "next.focus();"
                        "}"
                    "}"
                "}else if(myLength==0){"
                    "var previous=target.previousElementSibling;"
                    "if(previous!=null){"
                        "if(previous.className.includes(\"IP\")){"
                            "previous.focus();"
                        "}"
                    "}"
                "}"
            "}"
            "function ipFieldFocus(e){"
                "console.log(\"focus\");"
                "console.log(e);"
                "var target=e.srcElement||e.target;"
                "target.select();"
            "}"
            "function load(){"
                "var containers=document.getElementsByClassName(\"IP\");"
                "for(var i=0;i<containers.length;i++){"
                    "var container=containers[i];"
                    "container.oninput=switchIpField;"
                    "container.onfocus=ipFieldFocus;"
                "}"
                "containers=document.getElementsByClassName(\"tIP\");"
                "for(var i=0;i<containers.length;i++){"
                    "var container=containers[i];"
                    "container.oninput=switchIpField;"
                    "container.onfocus=ipFieldFocus;"
                "}"
                "toggleStaticIPFields();"
            "}"
            "function toggleStaticIPFields(){"
                "var enabled=document.getElementById(\"staticIP\").checked;"
                "document.getElementById(\"staticIPHidden\").disabled=enabled;"
                "var staticIpFields=document.getElementsByClassName('tIP');"
                "for(var i=0;i<staticIpFields.length;i++){"
                    "staticIpFields[i].disabled=!enabled;"
                "}"
            "}"
            "function restart() {"
                "window.location.href = '/restart';"
            "}"
            "function networkClick() {"
                "window.location.href = '/networks';"
            "}"
            "function updateBrightness(value) {"
              "document.getElementById('brightnessValue').innerText = value;"
              "fetch(`/setBrightness?value=${value}`);"
            "}"
            "function toggleLED() {"
              "fetch(`/toggleLED`);"
            "}"
        "</script>"
        "<body onload=\"load()\">"
            "<h1>LED setup</h1>"
            "<div class=\"container\">"
                "<p>Adjust the slider to change LED brightness:</p>"
                "<input type=\"range\" id=\"brightness\" min=\"0\" max=\"255\" value=\"128\" oninput=\"updateBrightness(this.value)\">"
                "<p>Brightness: <span id=\"brightnessValue\">128</span></p>"
                "<button type=\"button\" onclick=\"toggleLED()\">ON / OFF</button>"
                "<hr>"
                "<p><strong>Connection Status:</strong>" + getConnectionStatusString() + "</p>"
                "<p><strong>Network name (SSID):</strong>" + getSSID() + "</p>"
                "<p><strong>Signal strength:</strong>" + WiFi.RSSI() + " dBm</p>"
                "<p><strong>Static IP:</strong>" + (settings.staticIP == true ? "True" : "False") + "</p>"
                "<p><strong>This device IP:</strong><a href=\"http://" + WiFi.localIP().toString() + "\">" + WiFi.localIP().toString() + "</a></p>"
                "<p><strong>mDNS address:</strong> <a href=\"http://" + String(settings.ledName) + ".local\">http://" + String(settings.ledName) + ".local</a></p>"
                "<p><strong>Subnet mask:</strong>" + WiFi.subnetMask().toString() + "</p>"
                "<p><strong>Gateway:</strong>" + WiFi.gatewayIP().toString() + "</p>"
                "<button type=\"button\" onclick=\"restart()\">Restart</button>"
            "</div>"
            "<div class=\"container\">"
                "<h2>Settings</h2>"
                "<hr>"
                "<form action=\"/save\"method=\"post\">"
                    "<label>LED Light name:</label>"
                    "<input type=\"text\"maxlength=\"30\"name=\"tName\"value=\"" + String(WiFi.getHostname()) + "\"required/>"
                    "<hr>"
                    "<label>Network name(SSID):</label>"
                    "<input type =\"text\"maxlength=\"30\"name=\"ssid\"value=\"" +  getSSID() + "\"required/>"
                    "<label>Network password:</label>"
                    "<input type=\"password\"maxlength=\"30\"name=\"pwd\"pattern=\"^$|.{8,32}\"value=\"" +  WiFi.psk() + "\"/>"
                    "<br>"
                    "<button type=\"button\" onclick=\"networkClick()\">See available networks</button>"
                    "<hr>"
                    "<label>Use static IP:</label>"
                    "<input type=\"hidden\"id=\"staticIPHidden\"name=\"staticIP\"value=\"false\"/>"
                    "<input id=\"staticIP\"type=\"checkbox\"name=\"staticIP\"value=\"true\"onchange=\"toggleStaticIPFields()\"" + (settings.staticIP == true ? "checked" : "") + "/>"
                    "<label>This device IP:</label>"
                    "<div class=\"ip-fields\">"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"tIP1\"pattern=\"\\d{0,3}\"value=\"" + settings.myIP[0] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"tIP2\"pattern=\"\\d{0,3}\"value=\"" + settings.myIP[1] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"tIP3\"pattern=\"\\d{0,3}\"value=\"" + settings.myIP[2] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"tIP4\"pattern=\"\\d{0,3}\"value=\"" + settings.myIP[3] + "\"required/>"
                    "</div>"
                    "<label>Subnet mask:</label>"
                    "<div class=\"ip-fields\">"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"mask1\"pattern=\"\\d{0,3}\"value=\"" + settings.mySubnetMask[0] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"mask2\"pattern=\"\\d{0,3}\"value=\"" + settings.mySubnetMask[1] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"mask3\"pattern=\"\\d{0,3}\"value=\"" + settings.mySubnetMask[2] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"mask4\"pattern=\"\\d{0,3}\"value=\"" + settings.mySubnetMask[3] + "\"required/>"
                    "</div>"
                    "<label>Gateway:</label>"
                    "<div class=\"ip-fields\">"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"gate1\"pattern=\"\\d{0,3}\"value=\"" + settings.myGateway[0] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"gate2\"pattern=\"\\d{0,3}\"value=\"" + settings.myGateway[1] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"gate3\"pattern=\"\\d{0,3}\"value=\"" + settings.myGateway[2] + "\"required/>"
                        "<input class=\"tIP\"type=\"text\"maxlength=\"3\"name=\"gate4\"pattern=\"\\d{0,3}\"value=\"" + settings.myGateway[3] + "\"required/>"
                    "</div>"
                    "<input type=\"submit\"value=\"Save Changes\"/>"
                "</form>"
            "</div>"
            "<div class=\"container\">"
                "<h2>Firmware update</h2>"
                "<hr>"
                "<form method='POST' action='/upload' enctype='multipart/form-data'>"
                    "<input type='file' name='firmware'>"
                    "<input type='submit' value='Update Firmware'>"
                "</form>"
            "</div>"
            + htmlFooter +
        "</body>"
    "</html>");
}

// Funkcja obsługująca zmianę jasności diody LED na podstawie przesłanego ze strony parametru
void handleSetBrightness() {
  if (server.hasArg("value")) {                             // Sprawdzenie, czy parametr "value" został przesłany
    settings.ledBrightness = server.arg("value").toInt();   // Przypisanie wartości parametru do zmiennej jasności diody LED
    settings.ledBrightness = constrain(settings.ledBrightness, 0, 255); // Sprawdzenie, czy wartość mieści się w zakresie 0-255 i ewentualne ograniczenie wartości
    if (settings.ledEnabled) {                              // Sprawdzenie, czy dioda ma być włączona
      ledcWrite(ledChannel, settings.ledBrightness);        // Ustawienie jasności diody LED
    }
    server.send(200, "text/plain", "OK"); // Wysłanie odpowiedzi do klienta
  } else {
    server.send(400, "text/plain", "Missing value"); // Wysłanie odpowiedzi o błędzie do klienta
  }
}

// Zmienna zawierająca nagłówek i część kodu HTML z dostępnymi sieciami WiFi
String networkChoiseSiteHead    = 
    "<!DOCTYPE html><html>" + htmlHead(false) + 
    "<script>"
        "function backClick() {"
                "window.location.href = '/';"
            "}"
        "</script>"
    "<body>"
        "<div class=\"container\">"
            "<h2>Set new network credentials</h2>"
            "<hr>"
            "<form method='post' action='/save_network'>"
                "<div>"
                "<label for='ssid'>SSID</label>"
                "<input type='text' id='ssid' name='ssid'>"
                "</div>"
                "<div>"
                "<label for='passVal'>Password</label>"
                "<input type='password' id='passVal' name='password'>"
                "</div>"
                "<button type='submit'>Connect</button>"
                "<button type=\"button\" onclick=\"backClick()\">Cancel and get back</button>"
            "</form>"
        "</div>";

// Funkcja zwracająca listę dostępnych sieci WiFi
String listVisibleNetworks() {
  String networks = "<div class=\"container\">"
            "<h2>Available Networks</h2>"
            "<hr>";
  int numNetworks = WiFi.scanNetworks();    // Skanowanie i przypisanie liczby dostępnych sieci WiFi
  for (int i = 0; i < numNetworks; ++i) {   // Pętla iterująca po dostępnych sieciach WiFi
    networks += "<button type='button' onclick='copyText(this)'>" + WiFi.SSID(i) + "</button>"; // Dodanie przycisku z nazwą kolejnej dostępnej sieci WiFi
  }
  networks += "</div>";
  return networks; // Zwrócenie kodu HTML z listą dostępnych sieci WiFi
}

// Zmienna zawierająca stopkę HTML stron z dostępnymi sieciami WiFi
String networkChoiseSiteFooter  = htmlFooter +
      "<script>"
        "function copyText(element) {"
          "var textToCopy = element.textContent || element.innerText;"
          "document.getElementById('ssid').value = textToCopy;"
        "}"
      "</script></body></html>";

// Funkcja zwracająca cały kod HTML strony z dostępnymi sieciami WiFi
String getAvailableNetworksHtml() {
  return networkChoiseSiteHead + listVisibleNetworks() + networkChoiseSiteFooter;
}

// Funkcja obsługująca podstronę z dostępnymi sieciami WiFi
void handleNetworks() {
  server.send(200, "text/html", getAvailableNetworksHtml());
}

// Funkcja obsługująca przełączanie diody LED
void handleToggleLED() {
  if (settings.ledEnabled) {                        // Sprawdzenie, czy dioda jest włączona
    Serial.println("LED turned off");               // Wypisanie informacji o wyłączeniu diody LED do UART
    settings.ledEnabled = false;                    // Ustawienie zmiennej informującej o stanie diody LED na wyłączony
    ledcWrite(ledChannel, 0);                       // Wyłączenie diody LED
  } else {                                          // W przypadku gdy dioda jest wyłączona
    Serial.println("LED turned on");                // Wypisanie informacji o włączeniu diody LED do UART
    ledcWrite(ledChannel, settings.ledBrightness);  // Włączenie diody LED
    settings.ledEnabled = true;                     // Ustawienie zmiennej informującej o stanie diody LED na włączony
  }
  server.send(200, "text/plain", "OK");             // Wysłanie odpowiedzi do klienta
}

// Funkcja obsługująca zapis ustawień
void handleSave() {
    if (server.method() != HTTP_POST) { // Sprawdzenie, czy metoda zapytania to POST
        // Jeśli nie, to zwróć błąd 405 i informację o niedozwolonym zapytaniu
        server.send(405, "text/html", "<!DOCTYPE html><html>" + htmlHead(true) + 
            "<body><div class=\"container\">"
                "<h2>Error</h2>"
                "<hr>"
                "<p>Request without posting settings not allowed<br><br>Redirecting to main page...</p>"
            "</div></body></html>");
        return; // Zakończ funkcję
    }

    String ssid;                                    // Zmienna przechowująca nazwę sieci WiFi
    String pwd;                                     // Zmienna przechowująca hasło do sieci WiFi
    bool change = false;                            // Flaga informująca o zmianie ustawień
    for (uint8_t i = 0; i < server.args(); i++) {   // Pętla iterująca po przesłanych parametrach
        change = true;                              // Ustawienie flagi zmiany ustawień na true
        String var = server.argName(i);             // Przypisanie nazwy parametru do zmiennej
        String val = server.arg(i);                 // Przypisanie wartości parametru do zmiennej

        if (var == "tName") {                       // Jeśli nazwa parametru to "tName"
            val.toCharArray(settings.ledName, (uint8_t)32); // Skopiuj wartość parametru do zmiennej przechowującej nazwę urządzenia
        } else if (var == "ledBright") {            // Jeśli nazwa parametru to "ledBright"
            settings.ledBrightness = val.toInt();   // Skonwertuj wartość parametru na liczbę i przypisz do zmiennej jasności diody LED
        } else if (var == "ssid") {                 // Jeśli nazwa parametru to "ssid"
            ssid = String(val);                     // Przypisz wartość parametru do zmiennej przechowującej nazwę sieci WiFi
        } else if (var == "pwd") {                  // Jeśli nazwa parametru to "pwd"
            pwd = String(val);                      // Przypisz wartość parametru do zmiennej przechowującej hasło do sieci WiFi
        } else if (var == "staticIP") {             // Jeśli nazwa parametru to "staticIP"
            settings.staticIP = (val == "true");    // Przypisz wartość parametru do zmiennej informującej o użyciu stałego adresu IP
        } else if (var == "tIP1") {                 // Jeśli nazwa parametru to "tIP1"
            settings.myIP[0] = val.toInt();         // Skonwertuj wartość parametru na liczbę i przypisz do pierwszego bajtu adresu IP
        } else if (var == "tIP2") {                 // Jeśli nazwa parametru to "tIP2"
            settings.myIP[1] = val.toInt();         // Skonwertuj wartość parametru na liczbę i przypisz do drugiego bajtu adresu IP
        } else if (var == "tIP3") {                 // Jeśli nazwa parametru to "tIP3"
            settings.myIP[2] = val.toInt();         // Skonwertuj wartość parametru na liczbę i przypisz do trzeciego bajtu adresu IP
        } else if (var == "tIP4") {                 // Jeśli nazwa parametru to "tIP4"
            settings.myIP[3] = val.toInt();         // Skonwertuj wartość parametru na liczbę i przypisz do czwartego bajtu adresu IP
        } else if (var == "mask1") {                // Jeśli nazwa parametru to "mask1"
            settings.mySubnetMask[0] = val.toInt(); // Skonwertuj wartość parametru na liczbę i przypisz do pierwszego bajtu maski podsieci
        } else if (var == "mask2") {                // Jeśli nazwa parametru to "mask2"
            settings.mySubnetMask[1] = val.toInt(); // Skonwertuj wartość parametru na liczbę i przypisz do drugiego bajtu maski podsieci
        } else if (var == "mask3") {                // Jeśli nazwa parametru to "mask3"
            settings.mySubnetMask[2] = val.toInt(); // Skonwertuj wartość parametru na liczbę i przypisz do trzeciego bajtu maski podsieci
        } else if (var == "mask4") {                // Jeśli nazwa parametru to "mask4"
            settings.mySubnetMask[3] = val.toInt(); // Skonwertuj wartość parametru na liczbę i przypisz do czwartego bajtu maski podsieci
        } else if (var == "gate1") {                // Jeśli nazwa parametru to "gate1"
            settings.myGateway[0] = val.toInt();    // Skonwertuj wartość parametru na liczbę i przypisz do pierwszego bajtu bramy sieciowej
        } else if (var == "gate2") {                // Jeśli nazwa parametru to "gate2"
            settings.myGateway[1] = val.toInt();    // Skonwertuj wartość parametru na liczbę i przypisz do drugiego bajtu bramy sieciowej
        } else if (var == "gate3") {                // Jeśli nazwa parametru to "gate3"
            settings.myGateway[2] = val.toInt();    // Skonwertuj wartość parametru na liczbę i przypisz do trzeciego bajtu bramy sieciowej
        } else if (var == "gate4") {                // Jeśli nazwa parametru to "gate4"
            settings.myGateway[3] = val.toInt();    // Skonwertuj wartość parametru na liczbę i przypisz do czwartego bajtu bramy sieciowej
        }
    }

    if (change) {                   // Jeśli zmieniono ustawienia
        EEPROM.put(0, settings);    // Zapisz ustawienia do pamięci EEPROM
        EEPROM.commit();            // Zapisz zmiany w pamięci EEPROM

        // Wysłanie odpowiedzi do klienta
        server.send(200, "text/html", (String)"<!DOCTYPE html><html>" + htmlHead(true) + 
        "<body><div class=\"container\">"
            "<h2>Settings saved successfully</h2>"
            "<hr>"
            "<p>Redirecting to main page...</p>"
        "</div></body></html>");

        server.close();             // Zamknięcie połączenia z klientem
        delay(100);                 // Opóźnienie w celu zapisania danych i wysłania odpowiedzi do klienta

        WiFi.mode(WIFI_STA);        // Zmiana trybu pracy modułu WiFi na STATION
        delay(100);                 // Opóźnienie w celu zmiany trybu pracy modułu WiFi

        if (ssid && pwd) {          // Jeśli przesłano nazwę sieci WiFi i hasło
            WiFi.persistent(true);  // Włączenie zapisywania ustawień WiFi
            WiFi.begin(ssid.c_str(), pwd.c_str(), 0, NULL, false); // Próba połączenia z nową siecią WiFi
        }

        delay(100);                 // Opóźnienie w celu zmiany trybu pracy modułu WiFi
        ESP.restart();              // Restart modułu ESP
    }
}

// Funkcja obsługująca przechwytywanie przesyłanego oprogramowania
void handleFirmwareUpload() {
    HTTPUpload& upload = server.upload();                                           // Przypisanie przesłanych danych do zmiennej upload
    if (upload.status == UPLOAD_FILE_START) {                                       // Jeśli rozpoczęto przesyłanie pliku
        Serial.printf("Updating Firmware: %s\n", upload.filename.c_str());          // Wypisanie informacji o rozpoczęciu aktualizacji oprogramowania do UART
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {                                   // Rozpoczęcie aktualizacji oprogramowania i sprawdzenie czy funkcja zwróciła błąd
            Update.printError(Serial);                                              // Wypisanie informacji o błędzie aktualizacji do UART
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {                                // Jeśli przesyłany plik jest zapisywany
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {   // Zapisanie przesyłanych danych i sprawdzenie czy zapisano tyle danych ile przesłano
            Update.printError(Serial);                                              // Wypisanie informacji o błędzie aktualizacji do UART
        }
    } else if (upload.status == UPLOAD_FILE_END) {                                  // Jeśli przesyłanie pliku zakończono
        if (Update.end(true)) {                                                     // Zakończenie aktualizacji oprogramowania i sprawdzenie czy zakończono poprawnie
            Serial.printf("Update Success: %u bytes\n", upload.totalSize);          // Wypisanie informacji o zakończeniu aktualizacji do UART
        } else {                                                                    // Jeśli aktualizacja zakończyła się błędem
            Update.printError(Serial);                                              // Wypisanie informacji o błędzie aktualizacji do UART
        }
    }
}

// Funkcja obsługująca aktualizację oprogramowania
void handleFirmwareUpdate() {
    if (!Update.hasError()) {               // Sprawdzenie, czy aktualizacja oprogramowania nie zakończyła się błędem
        Serial.println("Restarting...");    // Wypisanie informacji o restarcie do UART
        // Wysłanie odpowiedzi do klienta
        server.send(200, "text/html", "<!DOCTYPE html><html>" + 
            htmlHead(true) + 
            "<body><div class=\"container\">"
                "<h2>Successfully updated</h2>"
                "<hr>"
                "<p>Redirecting to main page...</p>"
            "</div></body></html>");
        delay(100);                         // Opóźnienie w celu zapisania danych i wysłania odpowiedzi do klienta
        ESP.restart();                      // Restart modułu ESP
    } else {                                // Jeśli aktualizacja oprogramowania zakończyła się błędem
        // Wysłanie odpowiedzi do klienta
        server.send(500, "text/html", "<!DOCTYPE html><html>" + 
            htmlHead(true) + 
            "<body><div class=\"container\">"
                "<h2>Firmware Update Failed!</h2>"
                "<hr>"
                "<p>Redirecting to main page...</p>"
            "</div></body></html>");
    }
}

void handleButton() {
    int buttonState = digitalRead(BUTTON_PIN);  // Odczytanie stanu przycisku
    if (buttonState != lastButtonState) {       // Sprawdzenie, czy stan przycisku się zmienił
        lastDebounceTime = millis();            // Zapisanie czasu ostatniej zmiany stanu przycisku
    }
    if ((millis() - lastDebounceTime) > debounceDelay) { // Sprawdzenie, czy upłynął czas od ostatniej zmiany stanu przycisku
        if (buttonState == LOW && !buttonPressed) {      // Sprawdzenie, czy przycisk jest wciśnięty i czy nie był wcześniej wciśnięty
            buttonPressed = true;                        // Ustawienie zmiennej informującej o wciśnięciu przycisku na true
            Serial.println("Button pressed!");           // Wypisanie informacji o wciśnięciu przycisku do UART
            handleToggleLED();                           // Wywołanie funkcji obsługującej przełączanie diody LED
        } else if (buttonState == HIGH) {                // Jeśli przycisk nie jest wciśnięty
            buttonPressed = false;                       // Ustawienie zmiennej informującej o wciśnięciu przycisku na false
        }
    }
  lastButtonState = buttonState;                // Zapisanie stanu przycisku
}

// Funkcja inicializująca moduł ESP wykonywana raz po uruchomieniu modułu
void setup() {
  Serial.begin(115200);                             // Inicjalizacja komunikacji szeregowej z prędkością 115200 bitów na sekundę
  // Wypisanie informacji o starcie do UART
  Serial.println("########################");
  Serial.println("Serial started");

  EEPROM.begin(sizeof(settings));                   // Inicjalizacja pamięci EEPROM
  EEPROM.get(0, settings);                          // Odczytanie ustawień z pamięci EEPROM

  ledcSetup(ledChannel, freq, resolution);          // Konfiguracja PWM diody LED
  ledcAttachPin(PWM_PIN, ledChannel);               // Podpięcie diody LED do kanału PWM
  if (settings.ledEnabled) {                        // Sprawdzenie, czy dioda LED ma być włączona
    ledcWrite(ledChannel, settings.ledBrightness);  // Ustawienie jasności diody LED
  } else {                                          // Jeśli dioda LED ma być wyłączona
    ledcWrite(ledChannel, 0);                       // Wyłączenie diody LED
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);                // Ustawienie pinu przycisku jako wejście z wewnętrznym podciąganiem do VCC

  if (settings.staticIP && settings.myIP != IPADDR_NONE) {  // Sprawdzenie, czy ustawiono stały adres IP
    WiFi.config(settings.myIP, settings.myGateway, settings.mySubnetMask); // Ustawienie stałego adresu IP
  } else {                                                  // Jeśli nie ustawiono stałego adresu IP
    settings.staticIP = false;                              // Ustawienie zmiennej informującej o użyciu stałego adresu IP na false
  }

  WiFi.mode(WIFI_STA);                  // Ustawienie trybu pracy modułu WiFi na STATION
  WiFi.setHostname(settings.ledName);   // Ustawienie nazwy hosta modułu WiFi
  WiFi.setAutoReconnect(true);          // Włączenie automatycznego ponownego łączenia z siecią WiFi
  WiFi.begin();                         // Rozpoczęcie łączenia z siecią WiFi

  if (!MDNS.begin(settings.ledName)) {                  // Inicjalizacja mDNS i sprawdzenie czy wystąpił błąd
    Serial.println("Error setting up mDNS responder!"); // Wypisanie informacji o błędzie do UART
  } else {                                              // Jeśli inicjalizacja mDNS zakończyła się sukcesem
    Serial.println("mDNS responder started");           // Wypisanie informacji o sukcesie do UART
  }

  wifi_config_t conf;                       // Zmienna przechowująca konfigurację WiFi
  esp_wifi_get_config(WIFI_IF_STA, &conf);  // Pobranie konfiguracji WiFi

  // Wypisanie informacji o konfiguracji WiFi do UART
  Serial.println("------------------------");
  Serial.println("Connecting to WiFi...");
  Serial.println("Network name (SSID): " + getSSID());


  // Zgłoszenie do serwera HTTP obsługi różnych ścieżek
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/upload", HTTP_POST, handleFirmwareUpdate, handleFirmwareUpload);
  server.on("/networks", handleNetworks);
  server.on("/setBrightness", handleSetBrightness);
  server.on("/toggleLED", handleToggleLED);
  server.on("/save_network", HTTP_POST, [](){
        if (server.hasArg("ssid") && server.hasArg("password")) { // Sprawdzenie, czy przesłano nazwę sieci WiFi i hasło
            // Zapis przesłanych danych do zmiennych
            String ssid = String(server.arg("ssid"));
            String pwd = String(server.arg("password"));

            // Wysłanie odpowiedzi do klienta
            server.send(200, "text/html", "<!DOCTYPE html><html>" + htmlHead(false) + 
            "<body><div class=\"container\">"
                "<h2>Network changed</h2>"
                "<hr>"
                "<p>Please manually go to new IP address, or try to go <a href=\"http://" + 
                String(settings.ledName) + ".local\">http://" + 
                String(settings.ledName) + ".local</a> after connect to new saved network</p>"
            "</div></body></html>");
            
            WiFi.mode(WIFI_STA); // Włączenie trybu pracy modułu WiFi na STATION
            delay(100);          // Opóźnienie w celu zmiany trybu pracy modułu WiFi

            if (ssid && pwd) { // Jeśli przesłano nazwę sieci WiFi i hasło
                WiFi.persistent(true); // Włączenie zapisywania ustawień WiFi
                WiFi.begin(ssid.c_str(), pwd.c_str(), 0, NULL, false); // Próba połączenia z nową siecią WiFi
            }

            delay(100);     // Opóźnienie w celu zmiany trybu pracy modułu WiFi i zapisania danych
            ESP.restart();  // Restart modułu ESP
        } else {            // Jeśli nie przesłano nazwy sieci WiFi i hasła
            server.send(200, "text/html", "Wrong parameters"); // Wysłanie odpowiedzi o błędzie do klienta
        }
    });

  server.begin(); // Start serwera HTTP
  
  
  // Poczekaj na wynik pierwszej próby połączenia
  // To zapewni, że ​​softAP zostanie aktywowany tylko wtedy, gdy nie udało się połączyć,
  // a nie tylko dlatego, że nie miał jeszcze czasu tego zrobić.
  unsigned long start = millis(); // Zmienna przechowująca czas w milisekundach
  while((!WiFi.status() || WiFi.status() >= WL_DISCONNECTED) && (millis() - start) < 10000LU) { // Pętla oczekująca na połączenie z siecią WiFi
    delay(100); // Opóźnienie 100ms
  }

  firstRun = true;                  // Ustawienie flagi pierwszego uruchomienia na true
  state = STATE_CONNECTING_TO_WIFI; // Ustawienie stanu na łączenie z siecią WiFi
}

// Główna pętla programu wykonywana w nieskończoność
void loop() {
  handleButton();                           // Obsługa przycisku
  if (state == STATE_CONNECTING_TO_WIFI) {  // Jeśli stan to łączenie z siecią WiFi
    if (WiFi.status() == WL_CONNECTED) {    // Jeśli połączono z siecią WiFi
      WiFi.mode(WIFI_STA);                  // Ustawienie trybu pracy modułu WiFi na STATION
      // Wypisanie informacji o połączeniu z siecią WiFi do UART
      Serial.println("------------------------");
      Serial.println("Connected to WiFi:   " + getSSID());
      Serial.println("IP:                  " + WiFi.localIP().toString());
      Serial.println("mDNS address:        http://" + String(settings.ledName) + ".local");
      Serial.println("Subnet Mask:         " + WiFi.subnetMask().toString());
      Serial.println("Gateway IP:          " + WiFi.gatewayIP().toString());

      firstRun = true;                      // Ustawienie flagi pierwszego uruchomienia na true
      state = STATE_RUNNING;                // Ustawienie stanu na działanie
    } else if (firstRun) {                  // Jeśli flaga pierwszego uruchomienia jest ustawiona na true
      firstRun = false;                     // Ustawienie flagi pierwszego uruchomienia na false
      // Wypisanie informacji o nieudanym połączeniu z siecią WiFi do UART
      Serial.println("Unable to connect. Serving \"LED Light setup\" WiFi for configuration, while still trying to connect...");
      Serial.println("IP for that device in \"LED Light setup\" WiFi network is 192.168.4.1 or mDNS address http://" + String(settings.ledName) + ".local");
      WiFi.softAP("LED setup");             // Uruchomienie punktu dostępowego WiFi
      WiFi.mode(WIFI_AP_STA);               // Ustawienie trybu pracy modułu WiFi na AP+STA
    }
  }

  if (WiFi.status() != WL_CONNECTED && state != STATE_CONNECTING_TO_WIFI) { // Jeśli połączenie zostało utracone i stan nie jest łączenie z siecią WiFi
    // Wypisanie informacji o utraceniu połączenia z siecią WiFi do UART
    Serial.println("------------------------");
    Serial.println("WiFi connection lost...");
    firstRun = true;                        // Ustawienie flagi pierwszego uruchomienia na true
    state = STATE_CONNECTING_TO_WIFI;       // Ustawienie stanu na łączenie z siecią WiFi
  }

  server.handleClient();                    // Obsługa klientów serwera HTTP
}
