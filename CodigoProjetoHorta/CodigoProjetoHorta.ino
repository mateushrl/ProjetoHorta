#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Definição dos Pinos
#define SensorUmidade 34
#define bomba 16

#define Lampada 15

// Teclado Matricial
#define ROW_NUM 4
#define COLUMN_NUM 3

// Vriáveis
byte pin_rows[ROW_NUM] = {12, 27, 14, 26};
byte pin_column[COLUMN_NUM] = {25, 33, 32};
char key;
int valorTeclado = 0;
int sensorValue = 0;
int umidadeProgramada = 0;
String horaInicial = "";
float percentual = 0.0;
String aux = "";

String minutoAtual = "";
String horaAtual = "";

String tempoLigarLuz = "";
String tempoDesligarLuz = "";

char keys[ROW_NUM][COLUMN_NUM] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}};

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM);

TaskHandle_t xTaskHandleConfiguracao, xTaskHandleSensorUmidade, xTaskHandleIrrigacao, xTaskHandleLampada, xTaskHandleLCD, xTaskHandleLCD_Umidade, xTaskHandleApp;
TaskHandle_t xTimerIrrigacao, xTaskHandleTeclado, xTaskHandleRelogio;

// Protótipo das Tasks
void vTaskSensorUmidade(void *parameters);
void vTaskConfiguracao(void *parameters);
void vTaskLampada(void *parameters);
// void vTaskTeclado(void *parameters);
void vTaskRelogio(void *parameters);
void vTaskApp(void *parameters);

// Timers
void callBackIrrigacao(TimerHandle_t xTimerIrrigacao_Param);

// Semáfaros
SemaphoreHandle_t xSemaphore, xTeclado_semaphore, xSemaphoreLCD;

// Filas
QueueHandle_t xQueue_SensorUmidade, xQueue_Lampada, xQueue_LCD, xQueue_Relogio;

void setup()
{

  Serial.begin(115200);

  SerialBT.begin("Hortinha"); // Bluetooth device name

  while (!Serial)
  {
    ; /* Somente vai em frente quando a serial estiver pronta para funcionar */
  }

  // Define pinos
  pinMode(SensorUmidade, INPUT);
  pinMode(bomba, OUTPUT);
  pinMode(Lampada, OUTPUT);

  // Configuração do LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Criação das Filas
  xQueue_SensorUmidade = xQueueCreate(1, sizeof(float));
  xQueue_Lampada = xQueueCreate(1, sizeof(int));
  xQueue_LCD = xQueueCreate(1, sizeof(float));
  xQueue_Relogio = xQueueCreate(4, sizeof(char));

  // Criação dos Timers
  xTimerIrrigacao = xTimerCreate("TimerIrrigacao", pdMS_TO_TICKS(10000), pdFALSE, 0, callBackIrrigacao);

  // Criação dos semáfaros
  xSemaphore = xSemaphoreCreateMutex();

  xSemaphoreLCD = xSemaphoreCreateMutex();

  xTeclado_semaphore = xSemaphoreCreateMutex();

  // Criação das Tasks
  xTaskCreate(vTaskConfiguracao, "Configuracao", configMINIMAL_STACK_SIZE + 2048, NULL, 1, &xTaskHandleConfiguracao);
  xTaskCreate(vTaskSensorUmidade, "SensorUmidade", configMINIMAL_STACK_SIZE + 2048, NULL, 1, &xTaskHandleSensorUmidade);
  xTaskCreate(vTaskLampada, "Lampada", configMINIMAL_STACK_SIZE + 2048, NULL, 1, &xTaskHandleLampada);
  xTaskCreate(vTaskRelogio, "Relogio", configMINIMAL_STACK_SIZE + 2048, NULL, 1, &xTaskHandleRelogio);
  xTaskCreate(vTaskApp, "App", configMINIMAL_STACK_SIZE + 2048, NULL, 1, &xTaskHandleApp);
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(10));
}

void ImprimeLCD(String linha1, String linha2, boolean clear)
{
  xSemaphoreTake(xSemaphore, portMAX_DELAY);
  if (clear)
  {
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print(linha1);
    lcd.setCursor(1, 1);
    lcd.print(linha2);
  }
  else
  {
    lcd.setCursor(1, 1);
    lcd.print(linha2);
  }
  xSemaphoreGive(xSemaphore);
}

void vTaskApp(void *parameters)
{
  Serial.println("Entrou em APP");
  vTaskSuspend(NULL);
  String message = "";

  Serial.println("Resume em APP");
  while (1)
  {
    if (SerialBT.available())
    {

      char recebido = SerialBT.read();

      message += String(recebido);

      if (message == "a")
      {
        int enviarSinalLuz = 1;
        message.clear();
        xQueueOverwrite(xQueue_Lampada, (void *)&enviarSinalLuz);
      }
      else if (message == "s")
      {
        int enviarSinalLuz = 2;
        message.clear();
        xQueueOverwrite(xQueue_Lampada, (void *)&enviarSinalLuz);
      }
      else if (message == "l")
      {
        message.clear();
        digitalWrite(bomba, HIGH);
      }
      else if (message == "d")
      {
        message.clear();
        digitalWrite(bomba, LOW);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void vTaskSensorUmidade(void *parameters)
{
  Serial.println("Entrou em Umidade");
  vTaskSuspend(NULL);
  int sensorValue = 0;
  Serial.println("Resume em Umidade");
  while (1)
  {

    sensorValue = analogRead(SensorUmidade);

    if (sensorValue != 0)
    {
      percentual = (100 - (sensorValue * 100 / 4095));
    }

    if (percentual <= umidadeProgramada)
    {
      digitalWrite(bomba, HIGH);
      xTimerStart(xTimerIrrigacao, 0);
    }

    ImprimeLCD("Umidade:", String(percentual) + "%", true);

    Serial.println("Valor Percentual: " + String(percentual) + "%");

    vTaskDelay(pdMS_TO_TICKS(3500));
  }
}

// Liga a irrigação por 10 s
void callBackIrrigacao(TimerHandle_t xTimerIrrigacao_Param)
{
  digitalWrite(bomba, LOW);
}

void vTaskLampada(void *parameters)
{
  Serial.println("Entrou em Lampada");
  vTaskSuspend(NULL);
  int valorRecebidoLuz = 0;
  Serial.println("Resume em Lampada");

  while (1)
  {
    // Aguarda até receber mensagem na fila
    xQueueReceive(xQueue_Lampada, (void *)&valorRecebidoLuz, portMAX_DELAY);

    Serial.println("RECEBIDO: " + String(valorRecebidoLuz));

    if (valorRecebidoLuz == 1)
    {
      digitalWrite(Lampada, HIGH);
    }
    else if (valorRecebidoLuz == 2)
    {
      digitalWrite(Lampada, LOW);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void vTaskRelogio(void *parameters)
{
  Serial.println("Entrou em Relogio");

  vTaskSuspend(NULL);
  String horaRecebida = "";
  String horaAtualCompleta = "";
  String horaAtualLigaCompleta = "";
  String horaAtualDesligaCompleta = "";
  int enviarSinalLuz = 0;

  int hora = 0;
  int minuto = 0;
  Serial.println("Resume em Relogio");

  while (1)
  {

    if (horaInicial.length() == 4)
    {
      // Pega a hora atual inicial inserida e quebra em minutos e segundos e transforma em int
      if (horaAtualCompleta == "")
      {
        hora = horaInicial.substring(0, 2).toInt();
        minuto = horaInicial.substring(2, 4).toInt();
      }

      if (minuto == 59 && hora < 23)
      {
        minuto = 0;
        hora++;
      }

      if (hora >= 23 && minuto >= 59)
      {
        hora = 0;
        minuto = 0;
      }

      // Concatena com zero a esquerda para o formato da hora ficar correto -> 1:2 -> 01:02
      horaAtual = hora <= 9 ? "0" + String(hora) : String(hora);
      // Concatena com zero a esquerda para o formato da hora ficar correto -> 1:2 -> 01:02
      minutoAtual = minuto <= 9 ? "0" + String(minuto) : String(minuto);

      // Exibe a hora atual no formato -> 01:00
      horaAtualCompleta = horaAtual + ":" + minutoAtual;
      // Hora de ligar a luz
      horaAtualLigaCompleta = tempoLigarLuz.substring(0, 2) + ":" + tempoLigarLuz.substring(2, 4);
      // Hora de desligar a luz
      horaAtualDesligaCompleta = tempoDesligarLuz.substring(0, 2) + ":" + tempoDesligarLuz.substring(2, 4);

      // Liga a lampada enviado uma mensagem pela fila xQueue_Lampada
      if (horaAtualCompleta == horaAtualLigaCompleta)
      {
        Serial.println("Entrou para enviar sinal");
        enviarSinalLuz = 1;
        xQueueOverwrite(xQueue_Lampada, (void *)&enviarSinalLuz);
      }
      // Desliga a lampada enviado uma mensagem pela fila xQueue_Lampada
      if (horaAtualCompleta == horaAtualDesligaCompleta)
      {
        Serial.println("Entrou para desligar sinal");
        enviarSinalLuz = 2;
        xQueueOverwrite(xQueue_Lampada, (void *)&enviarSinalLuz);
      }

      vTaskSuspend(xTaskHandleSensorUmidade);

      vTaskDelay(pdMS_TO_TICKS(5000));

      vTaskResume(xTaskHandleSensorUmidade);

      // Debug
      Serial.println("Hora Atual: " + horaAtualCompleta);
      Serial.println("Hora Ligar: " + horaAtualLigaCompleta);
      Serial.println("Hora DEsligar: " + horaAtualDesligaCompleta);
    }
    minuto++;
    vTaskDelay(pdMS_TO_TICKS(55000));
  }
}

void vTaskConfiguracao(void *parameters)
{

  Serial.println("Entrou em Configuracao");

  String str = "";
  ImprimeLCD("Hora atual:", str, true);

  while (horaInicial.length() <= 3)
  {

    key = keypad.getKey();
    if (key)
    {
      if (String(key) == "*")
      {
        horaInicial.clear();
        ImprimeLCD("Hora atual:", str, true);
      }
      else
      {
        horaInicial += String(key);
        ImprimeLCD("Hora atual:", str, true);
      }
      ImprimeLCD("Hora atual:", horaInicial, false);

      Serial.println("Hora Atual: " + horaInicial);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ImprimeLCD("Ligar luz:", str, true);

  while (tempoLigarLuz.length() <= 3)
  {

    key = keypad.getKey();
    if (key)
    {
      if (String(key) == "*")
      {
        tempoLigarLuz.clear();
        ImprimeLCD("Ligar luz:", str, true);
      }
      else
      {
        tempoLigarLuz += String(key);
      }

      ImprimeLCD("Ligar luz: ", tempoLigarLuz, false);

      Serial.println("Liga Luz: " + tempoLigarLuz);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
  ImprimeLCD("Desligar luz: ", str, true);

  while (tempoDesligarLuz.length() <= 3)
  {

    key = keypad.getKey();
    if (key)
    {
      if (String(key) == "*")
      {
        tempoDesligarLuz.clear();
      }
      else
      {
        tempoDesligarLuz += String(key);
        ImprimeLCD("Desligar luz: ", str, true);
      }

      ImprimeLCD("Desligar luz: ", tempoDesligarLuz, false);

      Serial.println("Desliga Luz: " + tempoDesligarLuz);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ImprimeLCD("Umidade Min: ", str, true);

  while (aux.length() < 2)
  {
    key = keypad.getKey();

    if (key)
    {
      if (String(key) == "*")
      {
        aux.clear();
        ImprimeLCD("Umidade Min: ", str, true);
      }
      else
      {
        aux += String(key);
        umidadeProgramada = aux.toInt();

        ImprimeLCD("Umidade Min: ", aux, false);

        Serial.println("umidadeProgramada:  " + String(umidadeProgramada));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  vTaskResume(xTaskHandleSensorUmidade);
  vTaskResume(xTaskHandleRelogio);
  vTaskResume(xTaskHandleLampada);
  vTaskResume(xTaskHandleApp);
  vTaskDelete(NULL);
}
