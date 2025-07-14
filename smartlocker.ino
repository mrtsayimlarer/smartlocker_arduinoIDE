// Blynk kütüphanesi için projenizin şablon kimliği.
#define BLYNK_TEMPLATE_ID   "TMPLgDdfISZy"
// Blynk kütüphanesi için projenizin şablon adı.
#define BLYNK_TEMPLATE_NAME "Quickstart Template"

// --- Kütüphaneler ---
// Wi-Fi bağlantısı için temel kütüphane.
#include <WiFi.h>
// Wi-Fi üzerinden istemci bağlantısı için, Blynk için gereklidir.
#include <WiFiClient.h>
// ESP32 kartları için Blynk IoT platformu kütüphanesi.
#include <BlynkSimpleEsp32.h>
// I2C (Inter-Integrated Circuit) iletişimi için kütüphane, NFC modülü için gereklidir.
#include <Wire.h>
// Adafruit'in PN532 NFC/RFID okuyucu modülü için özel kütüphane.
#include <Adafruit_PN532.h>
// Fiziksel bir tuş takımı (Keypad) ile etkileşim kurmak için kütüphane.
#include <Keypad.h>
// ESP32'nin donanımsal rastgele sayı üretecisini (RNG) kullanmak için.
#include "esp_system.h"
// SHA256 karma (hash) algoritmasını kullanmak için mbedTLS kütüphanesinin bir parçası.
// Rastgele şifre üretirken entropiyi artırmak için kullanılır.
#include "mbedtls/sha256.h"

// --- Blynk ve WiFi Kimlik Bilgileri ---
// Blynk uygulamanızdan veya web arayüzünden aldığınız kimlik doğrulama token'ı.
char auth[] = "0UPSqYJ2Q14eqq0-0fOGCjFXH2FNhuWS";
// Wi-Fi ağınızın adı (SSID).
char ssid[] = "FiberHGW_HULA20";
// Wi-Fi ağınızın şifresi.
char pass[] = "g3dK9yHmEXEC";

// --- Blynk Sanal Pin Tanımlamaları ---
// Blynk uygulamasındaki V0 sanal pini, üretilen şifreyi göstermek için kullanılır (Labeled Value).
#define VIRTUAL_PIN_PASSWORD_DISPLAY V0
// Blynk uygulamasındaki V1 sanal pini, kapıyı uzaktan açmak için bir buton widget'ı ile ilişkilendirilir.
#define VIRTUAL_PIN_OPEN_DOOR_BUTTON V1
// Blynk uygulamasındaki V2 sanal pini, sistem durumu mesajlarını göstermek için kullanılır (Labeled Value).
#define VIRTUAL_PIN_STATUS_MESSAGE V2
// Blynk uygulamasındaki V3 sanal pini, kapı açık durumunu gösteren yeşil bir LED widget'ı ile ilişkilendirilir.
#define VIRTUAL_PIN_GREEN_LED V3
// Blynk uygulamasındaki V4 sanal pini, kapı kapalı durumunu gösteren kırmızı bir LED widget'ı ile ilişkilendirilir.
#define VIRTUAL_PIN_RED_LED V4

// --- Röle Mantığı Tanımlaması (Active LOW varsayımı: IN pinine LOW sinyali geldiğinde tetiklenir) ---
// Rölenin aktif olması (solenoidi çekmesi) için kontrol pininin alması gereken mantık seviyesi.
// Bu durumda, Active LOW olduğu için LOW (0V) sinyali gönderilir.
#define RELAY_ACTIVE_STATE LOW
// Rölenin pasif olması (solenoidi bırakması) için kontrol pininin alması gereken mantık seviyesi.
// Bu durumda, Active LOW olduğu için  (3.3V) sinyali gönderilir.
#define RELAY_INACTIVE_STATE HIGH

// --- Donanım Pin Tanımlamaları ---
// Rastgele sayı üretimi için kullanılan analog pin (boşta bir analog pin veya zener diyot bağlı pin).
// Bu pin, rastgeleliği artırmak için donanımsal gürültü toplar.
#define ZENER_PIN 34
// Röle modülünün kontrol pini (IN). ESP32'nin GPIO 23 numaralı pinine bağlıdır.
#define RELAY_PIN 23
// NFC modülü için I2C (Inter-Integrated Circuit) veri pini.
#define SDA_PIN 21
// NFC modülü için I2C saat pini.
#define SCL_PIN 22

// --- Keypad Tanımlamaları ---
// Tuş takımının satır ve sütun sayıları.
const byte ROWS = 4;
const byte COLS = 3;
// Tuş takımının fiziksel düzenini temsil eden 2D karakter dizisi.
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
// ESP32'deki GPIO pinleri, tuş takımının satırlarına bağlanır.
byte rowPins[ROWS] = {25, 13, 12, 27};
// ESP32'deki GPIO pinleri, tuş takımının sütunlarına bağlanır.
byte colPins[COLS] = {26, 32, 14};
// Keypad kütüphanesinden bir Keypad nesnesi oluşturulur.
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- Global Durum Değişkenleri ---
// Sistem tarafından dinamik olarak üretilen ve kullanıcıdan beklenen mevcut şifre.
String currentGeneratedPassword = "";
// Kullanıcının tuş takımından veya seri monitörden girdiği karakterler.
String userInput = "";
// Kapının kilitli (false) veya kilitsiz/açık (true) olup olmadığını belirten bayrak.
bool isDoorUnlocked = false;
// Kapının açıldığı zamanı milisaniye cinsinden kaydeden zaman damgası. Otomatik kapanma için kullanılır.
unsigned long doorUnlockTimestamp = 0;
// Kapının otomatik olarak kapanmadan önce açık kalacağı süre (3000 milisaniye = 3 saniye).
const long DOOR_UNLOCK_DURATION_MS = 3000;

// NFC (Yakın Alan İletişimi) okuma modunun aktif olup olmadığını belirten bayrak.
bool nfcActive = false;
// NFC okuma denemesi için zaman aşımı süresi.
unsigned long nfcTimeout = 0;
// NFC okuma deneme sayısı.
int nfcAttemptCount = 0;
// NFC okuma denemeleri için izin verilen maksimum sayı.
const int MAX_NFC_ATTEMPTS = 3;

// Güvenlik pini giriş modunun aktif olup olmadığını belirten bayrak.
bool securityPinMode = false;
// Sistem için belirlenmiş sabit güvenlik pini. Yanlış girişlerde devreye girer.
const String SECURITY_PIN = "000000";

// Adafruit PN532 kütüphanesini kullanarak NFC modülü için bir nesne oluşturulur.
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

// --- DOĞRU NFC KART UID TANIMI ---
// Yetkili NFC kartının benzersiz kimliği (UID). Lütfen bu değeri kendi kartınızın UID'si ile güncelleyin.
// Kartınızın UID'sini Seri Monitörden okuyabilirsiniz.
const uint8_t masterUID[] = {0x84, 0xE0, 0x3B, 0x05}; // ÖRNEK UID, KENDİ KARTINIZIN UID'si İLE DEĞİŞTİRİN
// masterUID dizisinin uzunluğunu otomatik olarak alır.
const int masterUID_len = sizeof(masterUID);

// --- Fonksiyon Tanımlamaları ---

// Zener diyottan veya boş analog pinden entropi toplayan fonksiyon.
// Daha rastgele sayılar üretmek için kullanılır.
uint32_t zener_entropy() {
  uint32_t noise = 0; // Toplanan gürültü değeri.
  for (int i = 0; i < 32; i++) {
    // Analog pinden okunan değerin en düşük anlamlı bitini (LSB) alarak rastgele bitler toplar.
    noise = (noise << 1) | (analogRead(ZENER_PIN) & 0x01);
    delayMicroseconds(50); // Okumalar arasında kısa bir gecikme ekleyerek rastgeleliği artırır.
  }
  return noise;
}

// Rastgele 6 haneli bir şifre oluşturan ve Blynk ile Seri Monitöre gönderen fonksiyon.
void generateAndDisplayNewPassword() {
  // Rastgelelik için ESP32'nin dahili RNG'si (esp_random()), zener diyottan gelen gürültü (zener_entropy())
  // ve mikrodenetleyicinin çalışma süresi (micros()) bir tohum (seed) olarak birleştirilir.
  uint32_t seed = esp_random() ^ zener_entropy() ^ micros();
  uint8_t hash[32]; // SHA256 karma çıktısı için 32 baytlık bir dizi.
  mbedtls_sha256_context ctx; // SHA256 bağlamı oluşturulur.
  mbedtls_sha256_init(&ctx); // SHA256 bağlamı başlatılır.
  mbedtls_sha256_starts(&ctx, 0); // SHA256 karma işlemi başlatılır (0 = SHA256).
  mbedtls_sha256_update(&ctx, (uint8_t*)&seed, sizeof(seed)); // Tohum değeri karma işlemine dahil edilir.
  mbedtls_sha256_finish(&ctx, hash); // Karma işlemi tamamlanır ve çıktı 'hash' dizisine yazılır.
  mbedtls_sha256_free(&ctx); // SHA256 bağlamı serbest bırakılır.

  // Karma çıktısından daha fazla rastgelelik elde etmek için bit manipülasyonu yapılır.
  uint32_t mixed = (*(uint32_t*)hash ^ 0xDEADBEEF) * 0x1337C0DE;
  mixed = (mixed >> 16) | (mixed << 16); // Bitler karıştırılır.

  char temp[7]; // 6 haneli şifre ve null sonlandırıcı için geçici bir karakter dizisi.
  // 'mixed' değerinin 1.000.000'e bölümünden kalanı alarak 0-999999 arasında bir sayı elde edilir.
  // %06lu formatı ile sayının 6 haneli olmasını ve baştaki sıfırların korunmasını sağlar.
  snprintf(temp, sizeof(temp), "%06lu", mixed % 1000000);
  currentGeneratedPassword = String(temp); // Oluşturulan şifre String formatına dönüştürülür.

  // Üretilen şifreyi Blynk uygulamasına ve Seri Monitöre gönderir.
  if (Blynk.connected()) {
    Blynk.virtualWrite(VIRTUAL_PIN_PASSWORD_DISPLAY, currentGeneratedPassword);
    Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Yeni Sifre Uretildi. Lutfen Girin.");
  }
  Serial.print("Yeni Uretilen Sifre (Blynk V0): ");
  Serial.println(currentGeneratedPassword);
  Serial.println("Lutfen bu sifreyi keypad veya Seri Monitorden girin.");

  // Yeni lanır.
  nfcAttemptCount = 0;
  nfcActive = false;
  securityPinMode = false;
}

// Kapıyı açma ve durumu güncelleme fonksiyonu (Röleyi tetikler).
void openDoor() {
  // Eğer kapı şu anda kilitli ise (yani isDoorUnlocked false ise).
  if (!isDoorUnlocked) { 
    // Röleyi aktif hale getirir (RELAY_ACTIVE_STATE değeri olan LOW sinyalini gönderir).
    // Bu, selenoid kilidin pimini çekecektir.
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_STATE); 
    Serial.print("Röle aktif edildi (PIN ");
    Serial.print(RELAY_PIN);
    Serial.print(" = ");
    Serial.print(RELAY_ACTIVE_STATE == LOW ? "LOW" : "HIGH"); // Gönderilen sinyali Seri Monitöre yazdırır.
    Serial.println(").");
    
    // Blynk uygulamasındaki LED'leri ve durum mesajını günceller.
    if (Blynk.connected()) {
      Blynk.virtualWrite(VIRTUAL_PIN_GREEN_LED, 255); // Blynk Yeşil LED'i AÇ (kapı açık).
      Blynk.virtualWrite(VIRTUAL_PIN_RED_LED, 0);     // Blynk Kırmızı LED'i KAPAT.
      Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Kapi Acildi!");
    }
    Serial.println("Kapı açıldı. 3 saniyelik geri sayım başladı.");
    doorUnlockTimestamp = millis(); // Kapının açıldığı anki milisaniye değerini kaydeder.
    isDoorUnlocked = true;          // Kapının açık olduğunu belirten bayrağı true yapar.
    // Kapı açıldığında tüm ilgili modları ve girişleri sıfırlar.
    nfcActive = false;              
    nfcAttemptCount = 0;            
    securityPinMode = false;        
    userInput = "";                 
  } else {
    Serial.println("Kapı zaten açık, tekrar açma komutu yoksayildi.");
  }
}

// Kapıyı kilitleme ve durumu güncelleme fonksiyonu (Röleyi pasifleştirir).
void closeDoor() {
  // Röleyi deaktif hale getirir (RELAY_INACTIVE_STATE değeri olan HIGH sinyali gönderilir).
  // Bu, selenoid kilidin pimini bırakarak kapıyı kilitler.
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_STATE); 
  Serial.print("Röle deaktif edildi (PIN ");
  Serial.print(RELAY_PIN);
  Serial.print(" = ");
  Serial.print(RELAY_INACTIVE_STATE == LOW ? "LOW" : "HIGH"); // Gönderilen sinyali Seri Monitöre yazdırır.
  Serial.println(").");
  
  // Blynk uygulamasındaki LED'leri ve durum mesajını günceller.
  if (Blynk.connected()) {
    Blynk.virtualWrite(VIRTUAL_PIN_GREEN_LED, 0);   // Blynk Yeşil LED'i KAPAT (kapı kilitli).
    Blynk.virtualWrite(VIRTUAL_PIN_RED_LED, 255);   // Blynk Kırmızı LED'i AÇ (kapı kilitli).
    Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Kapi Kilitlendi.");
  }
  Serial.println("Kapı kilitlendi.");
  isDoorUnlocked = false; // Kapının kilitli olduğunu belirten bayrağı false yapar.
  generateAndDisplayNewPassword(); // Kapı kilitlendikten sonra yeni bir şifre üretir.
}

// --- Blynk Buton Fonksiyonu (VIRTUAL_PIN_OPEN_DOOR_BUTTON) ---
// Blynk uygulamasındaki sanal buton V1'e basıldığında tetiklenen fonksiyon.
BLYNK_WRITE(VIRTUAL_PIN_OPEN_DOOR_BUTTON) {
  static bool buttonStatePrev = false; // Butonun önceki durumunu tutar (sadece basılma anını yakalamak için).
  int buttonState = param.asInt(); // Butonun anlık durumunu (0 veya 1) alır.

  // Buton basıldığında (yükselen kenar tetikleme: durum 1'e geçtiyse ve önceki durum 0 ise).
  if (buttonState == 1 && !buttonStatePrev) {
    if (!isDoorUnlocked) { // Kapı kilitli ise.
      Serial.println("Blynk butonu ile kapı açma isteği alındı.");
      openDoor(); // Kapıyı açma fonksiyonunu çağırır.
      if (Blynk.connected()) {
        Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Kapi Acildi! (Blynk Butonu)");
      }
    } else { // Kapı zaten açıksa.
      Serial.println("Kapi zaten acik (Blynk Butonu).");
      if (Blynk.connected()) {
        Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Kapi Zaten Acik.");
      }
    }
  }
  buttonStatePrev = (buttonState == 1); // Butonun son durumunu kaydeder.
  if (Blynk.connected()) {
    Blynk.virtualWrite(VIRTUAL_PIN_OPEN_DOOR_BUTTON, 0); // Blynk butonunu hemen sıfırlar (bir push butonu gibi davranmasını sağlar).
  }
}

// --- Setup Fonksiyonu ---
// ESP32'ye güç verildiğinde veya resetlendiğinde bir kez çalışır.
void setup() {
  Serial.begin(115200); // Seri iletişimi başlatır (bilgi mesajları ve hata ayıklama için).
  delay(1000);           // Sistem stabilizasyonu için kısa bir gecikme sağlar.

  // *** RÖLE PINİNİ BAŞLANGIÇTA GÜVENLİ HALE GETİRMEK İÇİN EN AGRESİF YAKLAŞIM ***
  // Röle Active LOW olduğu için, pasif durum HIGH'dır.
  // Pini OUTPUT olarak ayarlamadan önce bile kesinlikle HIGH'a çekmeye çalışıyoruz.
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_STATE); // 1. Adım: Pini hemen HIGH'a çek (röleyi pasif tut).
  pinMode(RELAY_PIN, OUTPUT);                    // 2. Adım: Pini çıkış olarak ayarla.
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_STATE); // 3. Adım: Çıkış olarak ayarladıktan soQnra tekrar HIGH'a çek (teyit et).
  
  Serial.print("Röle başlangıçta deaktif edildi (PIN ");
  Serial.print(RELAY_PIN);
  Serial.print(" = ");
  Serial.print(RELAY_INACTIVE_STATE == LOW ? "LOW" : "HIGH"); // Hangi sinyalin gönderildiğini Seri Monitöre yazdırır.
  Serial.println(").");
  isDoorUnlocked = false; // Sistem başladığında kapının kilitli olduğunu varsayarız.

  // Rölenin fiziksel olarak stabil hale gelmesi için kısa bir gecikme.
  // Bu gecikme Blynk bağlantı süresini biraz uzatabilir, ancak başlangıç stabilitesi için önemlidir.
  delay(500); 

  // Blynk'e Wi-Fi üzerinden bağlanmaya başlar.
  Serial.print("WiFi ve Blynk'e baglaniliyor: ");
  Blynk.begin(auth, ssid, pass);
  
  // Blynk bağlantısı kurulana kadar döngüde bekler ve Seri Monitöre noktalar yazdırır.
  while (Blynk.connected() == false) { 
    Serial.print(".");
    // Bağlantı beklenirken bile röle pinini sürekli olarak güvenli (pasif) durumda tut.
    digitalWrite(RELAY_PIN, RELAY_INACTIVE_STATE); 
    delay(500);
  }
  Serial.println("\nBlynk'e baglandi!");

  // --- Donanım Pinlerinin ve Modüllerin Diğer Başlatmaları ---
  
  // Zener pini (rastgelelik için) giriş olarak ayarlar.
  pinMode(ZENER_PIN, INPUT);
  // ESP32'nin analog okuma çözünürlüğünü 12 bite ayarlar (0-4095 arası değerler).
  analogReadResolution(12);

  // Keypad'in satır pinlerini dahili pull-up dirençleriyle giriş olarak ayarlar.
  for (int i = 0; i < ROWS; i++) {
    pinMode(rowPins[i], INPUT_PULLUP);
  }
  // Keypad'in sütun pinlerini çıkış olarak ayarlar ve LOW'a çeker.
  for (int i = 0; i < COLS; i++) {
    digitalWrite(colPins[i], LOW);
  }

  nfc.begin(); // NFC modülünü başlatır.
  // NFC modülünün firmware sürümünü kontrol ederek başarılı bir şekilde başlatılıp başlatılmadığını doğrular.
  if (!nfc.getFirmwareVersion()) {
    Serial.println("NFC modulu bulunamadi! Lutfen baglantilari kontrol edin.");
    if (Blynk.connected()) {
      Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "NFC HATA! Kontrol Edin.");
    }
    // Eğer NFC kritik bir bileşen ise, burada sonsuz bir döngüye girerek sistemin daha fazla ilerlemesini engelleyebilirsiniz: while (1);
  } else {
    Serial.println("NFC modulu bulundu.");
    nfc.SAMConfig(); // NFC modülünü SAM (Secure Access Module) modunda yapılandırır.
  }

  // Sistem başladığında ilk rastgele şifreyi üretir ve Blynk'e gönderir.
  generateAndDisplayNewPassword();
  // Blynk uygulamasındaki başlangıç durum LED'lerini ve mesajını ayarlar.
  if (Blynk.connected()) {
    Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Sistem Baslatildi. Sifre Bekleniyor.");
    Blynk.virtualWrite(VIRTUAL_PIN_GREEN_LED, 0);   // Yeşil LED KAPALI (kapı kilitli).
    Blynk.virtualWrite(VIRTUAL_PIN_RED_LED, 255);   // Kırmızı LED AÇIK (kapı kilitli).
  }
}

// --- Loop Fonksiyonu ---
// ESP32 çalıştığı sürece sürekli olarak çalışan ana program döngüsü.
void loop() {
  Blynk.run(); // Blynk kütüphanesinin arka planda çalışmasını sağlar. Bu, Wi-Fi bağlantısını sürdürür ve Blynk sunucusundan gelen/giden tüm iletişimleri yönetir.

  // Kapının otomatik kilitlenme mantığı.
  // Eğer kapı açıksa (isDoorUnlocked true) VE kapının açıldığı zamandan (doorUnlockTimestamp) bu yana
  // belirlenen süre (DOOR_UNLOCK_DURATION_MS) geçmişse.
  if (isDoorUnlocked && (millis() - doorUnlockTimestamp >= DOOR_UNLOCK_DURATION_MS)) {
    Serial.println("3 saniye doldu, kapi kilitleniyor...");
    closeDoor(); // Kapıyı kilitleme fonksiyonunu çağırır.
  }

  // Ek güvenlik kontrolü: Bu kısım, rölenin beklenmedik bir şekilde aktif olması durumunda müdahale etmek içindir.
  // Eğer kapı kilitli olması gerekirken (isDoorUnlocked false)
  // ancak röle fiziksel olarak aktif durumdaysa (digitalRead(RELAY_PIN) == RELAY_ACTIVE_STATE ise),
  // bu beklenmeyen bir durumdur ve röleyi tekrar güvenli (pasif) duruma getirir.
  // Bu kontrol, potansiyel bir donanımsal arıza veya yazılımsal hatayı düzeltmek içindir.
  // Örneğin, bir elektromanyetik gürültü pini geçici olarak LOW'a çekmiş olabilir.
  if (digitalRead(RELAY_PIN) == RELAY_ACTIVE_STATE && !isDoorUnlocked) { 
    Serial.println("UYARI: Röle beklenmedik şekilde aktif! Güvenlik için kapatılıyor.");
    closeDoor(); // Röleyi güvenli duruma çekmek için closeDoor() fonksiyonunu çağırır.
  }

  // Keypad girişlerini kontrol etme mantığı.
  // Bu kısım sadece kapı kilitliyken (!isDoorUnlocked), NFC okuma modu aktif değilken (!nfcActive)
  // ve güvenlik pini modu aktif değilken (!securityPinMode) çalışır.
  if (!isDoorUnlocked && !nfcActive && !securityPinMode) { 
    char key = keypad.getKey(); // Keypad'den basılan tuşu okur. Eğer tuşa basılmadıysa 0 (null) döner.
    if (key) { // Eğer bir tuşa basıldıysa.
      Serial.print("Keypad Tusu Algilandi: ");
      Serial.println(key); // Basılan tuşu Seri Monitöre yazdırır.

      if (key == '#') { // Kullanıcı onay tuşuna ('#') bastıysa.
        Serial.print("Girilen Sifre: ");
        Serial.println(userInput); // Kullanıcının girdiği şifreyi yazdırır.
        Serial.print("Beklenen Sifre: ");
        Serial.println(currentGeneratedPassword); // Beklenen şifreyi yazdırır.

        if (userInput == currentGeneratedPassword) { // Girilen şifre doğruysa.
          Serial.println("Sifre DOGRU! Kapi aciliyor...");
          openDoor(); // Kapıyı açma fonksiyonunu çağırır.
          if (Blynk.connected()) {
            Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Sifre Dogru! Kapi Acildi.");
          }
        } else { // Girilen şifre yanlışsa.
          Serial.println("Sifre YANLIS!");
          if (Blynk.connected()) {
            Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Sifre Yanlis! NFC Bekleniyor.");
          }
          nfcActive = true; // Şifre yanlışsa NFC okuma modunu aktifleştirir.
          nfcTimeout = millis() + 10000; // NFC okuması için 10 saniye zaman aşımı ayarlar.
          nfcAttemptCount = 0; // NFC deneme sayacını sıfırlar, yeni denemelere başlasın.
        }
        userInput = ""; // Kullanıcı girişini bir sonraki deneme için sıfırlar.
      } else if (key == '*') { // Kullanıcı sıfırlama tuşuna ('*') bastıysa.
        userInput = ""; // Kullanıcı girişini tamamen sıfırlar.
        Serial.println("Keypad Girisi Sifirlandi.");
        if (Blynk.connected()) {
          Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Giris Sifirlandi.");
        }
      } else { // Rakam tuşları basıldıysa.
        if (userInput.length() < 6) { // Şifre uzunluğunun 6 haneyi geçmemesini kontrol eder.
          userInput += key; // Basılan tuşu kullanıcı girişine ekler.
          Serial.print("Girilen Sifre (Parca): ");
          Serial.println(userInput);
        } else {
          Serial.println("Sifre uzunlugu maksimuma ulasti. '#' veya '*' basin.");
        }
      }
    }
  }
  
  // --- NFC okuma modu aktifse ve zaman aşımı dolmadıysa (ve kapı kilitliyse) ---
  if (nfcActive && millis() < nfcTimeout && !isDoorUnlocked) {
    uint8_t uid[7]; // Okunan NFC kartının UID'sini saklamak için bir dizi.
    uint8_t uidLength; // Okunan UID'nin uzunluğu.
    // NFC modülünden pasif bir hedef (kart) okumaya çalışır (MIFARE ISO14443A tipi).
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) { 
      String readUID_str = ""; 
      // Okunan UID'yi her baytı hexadecimal formatta içeren bir String'e dönüştürür.
      for (uint8_t i = 0; i < uidLength; i++) {
        readUID_str += String(uid[i] < 0x10 ? "0" : ""); // Tek haneli baytlar için öne sıfır ekler.
        readUID_str += String(uid[i], HEX); // Baytı hexadecimal olarak string'e ekler.
      }
      readUID_str.toLowerCase(); // Karşılaştırma tutarlılığı için tüm karakterleri küçük harfe dönüştürür.

      Serial.print("Okunan UID: ");
      Serial.println(readUID_str); // Okunan UID'yi Seri Monitöre yazdırır.

      bool uid_matched = true; // UID eşleşme durumu bayrağı.
      if (uidLength != masterUID_len) { // Okunan UID uzunluğu, kayıtlı master UID uzunluğuna uymuyorsa.
        uid_matched = false;
      } else {
        for (uint8_t i = 0; i < uidLength; i++) { // Her baytı tek tek karşılaştırır.
          if (uid[i] != masterUID[i]) { 
            uid_matched = false;
            break; // Eşleşmeyen bir bayt bulunursa döngüden çıkar.
          }
        }
      }

      if (uid_matched) { // UID eşleştiyse.
        Serial.println("NFC Dogrulandi! Kapi aciliyor...");
        openDoor(); // Kapıyı açma fonksiyonunu çağırır.
        if (Blynk.connected()) {
          Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "NFC Dogrulandi! Kapi Acildi.");
        }
        nfcActive = false; // NFC okuma modunu kapatır.
        nfcAttemptCount = 0; // NFC deneme sayacını sıfırlar.
      } else { // UID eşleşmediyse.
        Serial.println("Yetkisiz NFC karti!");
        if (Blynk.connected()) {
          Blynk.virtualWrite(VIRTUAL_PIN_PASSWORD_DISPLAY, "Yetkisiz NFC Kart!"); // Blynk'e yanlış kart uyarısı gönder
          Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Yetkisiz NFC Kart!");
        }
        nfcAttemptCount++; // NFC deneme sayacını artırır.
        Serial.print("NFC Deneme Sayisi: ");
        Serial.println(nfcAttemptCount);

        if (nfcAttemptCount >= MAX_NFC_ATTEMPTS) { // Maksimum deneme sayısına ulaşıldıysa.
          Serial.println("NFC deneme limiti asildi! Guvenlik pini bekleniyor.");
          if (Blynk.connected()) {
            Blynk.virtualWrite(VIRTUAL_PIN_PASSWORD_DISPLAY, "NFC Limit Asildi! Guvenlik Pini."); // Blynk'e limit aşımı uyarısı gönder
            Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "NFC Limit Asildi! Guvenlik Pini.");
          }
          nfcActive = false;       // NFC modunu kapatır.
          securityPinMode = true;  // Güvenlik pini modunu aktifleştirir.
          userInput = "";          // Kullanıcı girişini sıfırlar.
        }
        delay(1000); // Kısa bir gecikme sağlar (ard arda okumaları veya hızlı denemeleri engellemek için).
        nfcTimeout = millis() + 10000; // Yeni bir deneme için zaman aşımını yeniler.
      }
    }
  } else if (nfcActive && millis() >= nfcTimeout) { // NFC okuma zaman aşımı dolduysa.
    Serial.println("NFC zaman asimi.");
    if (Blynk.connected()) {
      Blynk.virtualWrite(VIRTUAL_PIN_PASSWORD_DISPLAY, "NFC Zaman Asimi. Yeni Sifre Girin."); 
      Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "NFC Zaman Asimi.");
    }
    nfcActive = false; // NFC modunu kapatır.
    nfcAttemptCount++; // Deneme sayacını artırır.
    Serial.print("NFC Deneme Sayisi: ");
    Serial.println(nfcAttemptCount);
    if (nfcAttemptCount >= MAX_NFC_ATTEMPTS) { // Maksimum deneme sayısına ulaşıldıysa.
      Serial.println("NFC deneme limiti asildi! Guvenlik pini bekleniyor.");
      if (Blynk.connected()) {
        Blynk.virtualWrite(VIRTUAL_PIN_PASSWORD_DISPLAY, "NFC Limit Asildi! Guvenlik Pini.");
        Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "NFC Limit Asildi! Guvenlik Pini.");
      }
      securityPinMode = true;  // Güvenlik pini modunu aktifleştirir.
      userInput = "";          // Kullanıcı girişini sıfırlar.
    } else {
      nfcActive = true; // Yeni bir deneme hakkı verir.
      nfcTimeout = millis() + 10000; // Zaman aşımını yeniler.
      Serial.println("NFC icin yeni deneme hakki verildi.");
      if (Blynk.connected()) {
        Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "NFC Icin Tekrar Deneyin.");
      }
    }
  }

  // --- Güvenlik pini modundaysa keypad girişlerini kontrol et (ve kapı kilitliyse) ---
  if (securityPinMode && !isDoorUnlocked) {
    char key = keypad.getKey(); // Keypad'den tuş okur.
    if (key) {
      Serial.print("Guvenlik Pini Tusu Algilandi: ");
      Serial.println(key);

      if (key == '#') { // Onay tuşu basıldıysa.
        Serial.print("Girilen Guvenlik Pini: ");
        Serial.println(userInput);
        Serial.print("Beklenen Guvenlik Pini: ");
        Serial.println(SECURITY_PIN);

        if (userInput == SECURITY_PIN) { // Girilen güvenlik pini doğruysa.
          Serial.println("Guvenlik Pini DOGRU! Kapi aciliyor...");
          openDoor(); // Kapıyı açma fonksiyonunu çağırır.
          if (Blynk.connected()) {
            Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Guvenlik Pini Dogru! Kapi Acildi.");
          }
        } else { // Girilen güvenlik pini yanlışsa.
          Serial.println("Guvenlik pini YANLIS! Sistem kilitlendi.");
          if (Blynk.connected()) {
            Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Guvenlik Pini Yanlis! Kilitlendi.");
          }
          delay(5000); // Yanlış güvenlik pini girildiğinde 5 saniye bekletir (ceza süresi).
          generateAndDisplayNewPassword(); // Yeni bir şifre üretir.
        }
        userInput = ""; // Kullanıcı girişini sıfırlar.
        securityPinMode = false; // Güvenlik pini modunu kapatır.
      } else if (key == '*') { // Sıfırlama tuşu basıldıysa.
        userInput = ""; // Güvenlik pini girişini sıfırlar.
        Serial.println("Guvenlik Pini Girisi Sifirlandi.");
        if (Blynk.connected()) {
          Blynk.virtualWrite(VIRTUAL_PIN_STATUS_MESSAGE, "Guvenlik Pini Girisi Sifirlandi.");
        }
      } else { // Rakam tuşları basıldıysa.
        if (userInput.length() < 6) { // Güvenlik pini uzunluğunun 6 haneyi geçmemesini sağlar.
          userInput += key; // Basılan tuşu kullanıcı girişine ekler.
          Serial.print("Girilen Guvenlik Pini (Parca): ");
          Serial.println(userInput);
        } else {
          Serial.println("Guvenlik pini uzunlugu maksimuma ulasti. '#' veya '*' basin.");
        }
      }
    }
  }
}