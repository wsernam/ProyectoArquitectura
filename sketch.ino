#include "StateMachineLib.h"
#include "AsyncTaskLib.h"
#include "DHT.h"
#include <LiquidCrystal.h>
#include <Keypad.h>

// Configuración del LCD
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// Configuración del teclado
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};
byte rowPins[ROWS] = {24, 26, 28, 30};
byte colPins[COLS] = {32, 34, 36, 38};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Configuración de sensores y pines
#define DHTPIN 46
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
#define TEMPERATURA_UMBRAL 30.0 // Temperatura De alerta
#define PIN_SENSOR_HALL A1  
#define BUZZER_PIN 43
#define LDR_PIN A0
#define PIN_PIR 48
#define LED_PIN 13

// Máquina de estados
enum State
{
  INICIO,
  BLOQUEADO,
  MONITOREO_AMBIENTAL,
  MONITOR_EVENTOS,
  ALARMA
};
enum Input
{
  INPUT_T,       // Evento temporizador terminado
  INPUT_P,       // Evento temperatura alta
  INPUT_S,       // Evento PIR o Hall
  INPUT_UNKNOWN // Evento predeterminado, sin acciones específicas
};
StateMachine stateMachine(5, 8);
Input input = INPUT_UNKNOWN;

// Variables globales
String contrasenaIngresada = "";
String contrasenaCorrecta = "1234";
float temperatura = 0.0, humedad = 0.0;
int luz = 0, pirEstado = 0;
//variable booleana que indica si una transición entre estados debe ocurrir en la maquina de estados
bool transicion_desencadenada = false;
//almacena el momento exacto en el que el estado del LED y buzzer cambio por ultima vez
unsigned long ultimo_Tiempo_Led = 0;
unsigned long ultimo_tiempo_buzzer = 0;
// intervalo predeterminado para el LED
unsigned long ledIntervalo = 500; 
bool ledEstado = false;
bool buzzerEstado = false;

// ====================== AsyncTasks ======================
AsyncTask TaskTemperatura(500, true, []() {
  temperatura = dht.readTemperature();
    humedad = dht.readHumidity();
    if (temperatura > TEMPERATURA_UMBRAL && stateMachine.GetState() == MONITOREO_AMBIENTAL)
    {
        input = INPUT_P;
        Serial.println("Temperatura excede el umbral. Activando alarma");
    }
});

AsyncTask TaskLuz(500, true, []() {
    luz = analogRead(LDR_PIN);
});

AsyncTask TaskInfraRojo(500, true, []() {
  pirEstado = digitalRead(PIN_PIR);
  if (pirEstado == HIGH && stateMachine.GetState() == MONITOR_EVENTOS){
    input = INPUT_S;
    Serial.println("Movimiento detectado. Activando alarma");
  }
});

AsyncTask TaskMonitoreoAmbiental(5000, false, []() {
  if (stateMachine.GetState() == MONITOREO_AMBIENTAL){
    transicion_desencadenada = true;
  }
});

AsyncTask TaskMonitorEventos(3000, false, []() {
  if (stateMachine.GetState() == MONITOR_EVENTOS){
    transicion_desencadenada = true;
  }
});

AsyncTask TaskBloqueoTiempo(7000, false, []() {
	if (stateMachine.GetState() == BLOQUEADO){
    input = INPUT_T; // Transición al estado INICIO
    Serial.println("Tiempo de bloqueo terminado. Regresando al estado INICIO.");
  }
});

// ====================== Funciones Estados ======================
//controla el parpadeo del LED en funcion de un intervalo de tiempo.
void MantenerLed(unsigned long interval){
  if (millis() - ultimo_Tiempo_Led >= interval){
    ultimo_Tiempo_Led = millis();
    ledEstado = !ledEstado;
    digitalWrite(LED_PIN, ledEstado);
  }
}
//controla el buzzer para crear un sonido de alarma.
void MantenerBuzzer(){
	// Alternar tono cada 150 ms
  if (millis() - ultimo_tiempo_buzzer >= 150){
    ultimo_tiempo_buzzer = millis();
    buzzerEstado = !buzzerEstado;
    if (buzzerEstado){
      tone(BUZZER_PIN, 1000); // Tono 1000 Hz
    }else{
      tone(BUZZER_PIN, 1500); // Tono 1500 Hz
    }
  }
}
//configuracion inicial al entrar en el estado BLOQUEADO.
void Bloqueado(){
  lcd.clear();
  lcd.print("BLOQUEADO 7s");
  Serial.println("Entrando al estado BLOQUEADO");
  ledIntervalo = 500; // Parpadeo cada 500 ms
  tone(BUZZER_PIN, 500); // Buzzer a 500 Hz constante
	TaskBloqueoTiempo.Start(); 
}
//configuracion al salir del estado BLOQUEADO.
void salir_Bloqueado(){
  Serial.println("Saliendo del estado BLOQUEADO");
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
	TaskBloqueoTiempo.Stop();// Detiene el temporizador
}
//configuracion inicial al entrar en el estado INICIO.
void Inicio(){
	pirEstado =0;
  input = INPUT_UNKNOWN;
  contrasenaIngresada = "";
  lcd.clear();
	lcd.print("Ingrese clave:");
	Serial.println("Entrando al estado INICIO");
}
//configuración al salir del estado INICIO.
void salir_Inicio(){
  Serial.println("Saliendo del estado INICIO");
  lcd.clear();
}
//configuracion inicial al entrar en el estado MONITOREO_AMBIENTAL.
void Monitoreo(){
  lcd.clear();
  // Leer sensores manualmente en el estado MONITOREO_AMBIENTAL
  temperatura = dht.readTemperature();
  humedad = dht.readHumidity();
  luz = analogRead(LDR_PIN);
  lcd.print("T: ");
  lcd.print(temperatura, 1);
  lcd.print("C H:");
  lcd.print(humedad, 1);
  lcd.setCursor(0, 1);
  lcd.print("Luz: ");
  lcd.print(luz);
  Serial.println("Entrando al estado MONITOREO_AMBIENTAL");
  TaskTemperatura.Start();
  TaskLuz.Start();
  TaskMonitoreoAmbiental.Start();
  transicion_desencadenada = false;
}
//configuracion al salir del estado MONITOREO_AMBIENTAL
void salir_Monitoreo(){
  Serial.println("Saliendo del estado MONITOREO_AMBIENTAL");
  TaskTemperatura.Stop();
  TaskLuz.Stop();
  TaskMonitoreoAmbiental.Stop();
  lcd.clear();
}
// configuracion inicial al entrar en el estado MONITOR_EVENTOS.
void Eventos(){
  lcd.clear();
  // Leer estado del botón (simulando el sensor Hall)
  int estadoBotonHall = digitalRead(PIN_SENSOR_HALL);
  // Leer estado del sensor PIR
  pirEstado = digitalRead(PIN_PIR);
  // Mostrar información en el LCD
  lcd.print("PIR: ");
  lcd.print(pirEstado ? "Activo" : "Inactivo");
  lcd.setCursor(0, 1);
  lcd.print("HALL: ");
  lcd.print(estadoBotonHall == HIGH ? "ALTO" : "BAJO");
  // Activar alarma si el botón está en HIGH mientras se implementa sensor hall
  if (estadoBotonHall == HIGH)
  {
  	input = INPUT_S;
    Serial.println("Campo magnético alto detectado. Activando alarma");
  }
  Serial.print("Estado del botón Hall: ");
  Serial.println(estadoBotonHall == HIGH ? "ALTO" : "BAJO");
  Serial.println("Entrando al estado MONITOR_EVENTOS");
  TaskInfraRojo.Start();
  TaskMonitorEventos.Start();
  transicion_desencadenada = false;
}
// configuracion al salir en el estado MONITOR_EVENTOS.
void salir_Eventos(){
  Serial.println("Saliendo del estado MONITOR_EVENTOS");
  TaskInfraRojo.Stop();
  TaskMonitorEventos.Stop();
  lcd.clear();
}
// configuracion inicial al entrar en el estado ALARMA.
void Alarma(){
  lcd.clear();
	if(temperatura >TEMPERATURA_UMBRAL){
		lcd.print("TEMP ALTA");
	}else{
		lcd.print("MOV DETECTADO");
	}
	delay(1500);
	lcd.clear();
  lcd.print("ALARMA ACTIVADA");
  Serial.println("Entrando al estado ALARMA");
  ledIntervalo = 150; // Parpadeo cada 150 ms
}
// configuracion al salir del estado ALARMA.
void salir_Alarma(){
  Serial.println("Saliendo del estado ALARMA");
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
  lcd.clear();
}
//leer_teclado
void leer_teclado(){
  char tecla = keypad.getKey();
  if (tecla){
    if (stateMachine.GetState() == ALARMA && tecla == '#'){
      input = INPUT_T;
      Serial.println("Tecla # presionada. Desactivando alarma");
      return;
    }
    if (stateMachine.GetState() == INICIO){
      if (tecla == '#'){
        if (contrasenaIngresada == contrasenaCorrecta){
          input = INPUT_T;
        }else{
          contrasenaIngresada = "";
          lcd.clear();
          lcd.print("Clave incorrecta");
          delay(1000);
          input = INPUT_S;
          return;
        }
      }else if (tecla == '*'){
        contrasenaIngresada = "";
      }else if (contrasenaIngresada.length() < 4){
        contrasenaIngresada += tecla;
			}
    	lcd.setCursor(0, 1);
    	lcd.print(contrasenaIngresada);
    }
  }
}
// ====================== Configuración Inicial ======================
void setup()
{
  Serial.begin(9600);
  lcd.begin(16, 2);
  dht.begin();
  pinMode(PIN_PIR, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  stateMachine.AddTransition(INICIO, MONITOREO_AMBIENTAL, []() { return input == INPUT_T; });
  stateMachine.AddTransition(MONITOREO_AMBIENTAL, MONITOR_EVENTOS, []() { return transicion_desencadenada; });
  stateMachine.AddTransition(MONITOR_EVENTOS, MONITOREO_AMBIENTAL, []() { return transicion_desencadenada; });
  stateMachine.AddTransition(MONITOREO_AMBIENTAL, ALARMA, []() { return input == INPUT_P; });
  stateMachine.AddTransition(MONITOR_EVENTOS, ALARMA, []() { return input == INPUT_S; });
  stateMachine.AddTransition(ALARMA, INICIO, []() { return input == INPUT_T; });
  stateMachine.AddTransition(INICIO, BLOQUEADO, []() { return input == INPUT_S; });
  stateMachine.AddTransition(BLOQUEADO, INICIO, []() { return input == INPUT_T; });

  stateMachine.SetOnEntering(INICIO, Inicio);
  stateMachine.SetOnLeaving(INICIO, salir_Inicio);
  stateMachine.SetOnEntering(BLOQUEADO, Bloqueado);
  stateMachine.SetOnLeaving(BLOQUEADO, salir_Bloqueado);
  stateMachine.SetOnEntering(MONITOREO_AMBIENTAL, Monitoreo);
  stateMachine.SetOnLeaving(MONITOREO_AMBIENTAL, salir_Monitoreo);
  stateMachine.SetOnEntering(MONITOR_EVENTOS, Eventos);
  stateMachine.SetOnLeaving(MONITOR_EVENTOS, salir_Eventos);
  stateMachine.SetOnEntering(ALARMA, Alarma);
  stateMachine.SetOnLeaving(ALARMA, salir_Alarma);

  stateMachine.SetState(INICIO, false, true);
}

// ====================== Bucle Principal ======================
void loop()
{
  leer_teclado();
	if (stateMachine.GetState() == ALARMA || stateMachine.GetState() == BLOQUEADO){
    MantenerLed(ledIntervalo);
  }
	if (stateMachine.GetState() == ALARMA){
    MantenerBuzzer();
  }
  TaskTemperatura.Update();
  TaskLuz.Update();
  TaskInfraRojo.Update();
  TaskMonitoreoAmbiental.Update();
  TaskMonitorEventos.Update();
  TaskBloqueoTiempo.Update();

  stateMachine.Update();
}