// Defina os pinos de LED e LDR
// Defina uma variável com valor máximo do LDR (4000)
// Defina uma variável para guardar o valor atual do LED (10)
int ledPin = 2;
int ldrPin = 34;
int ldrMax = 4095;
int ledValue = 10;
int thresholdValue = 500;

void setup() {
    Serial.begin(9600);
    
    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);

    ledUpdate(ledValue);
    
    Serial.printf("SmartLamp Initialized.\n");

}

// Função loop será executada infinitamente pelo ESP32
void loop() {
    //Obtenha os comandos enviados pela serial 
    //e processe-os com a função processCommand
     if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command.length() > 0) {
            processCommand(command);
        }
    }
}

void processCommand(String command) {
    if (command.startsWith("SET_LED ")) {
        String valorStr = command.substring(8);
        ledValue = valorStr.toInt(); 
        ledUpdate(ledValue);
        Serial.printf("RES SET_LED 1\n"); 
    }
    else if (command == "GET_LED") {
        Serial.printf("RES GET_LED %d\n", ledValue);
    }
    
    else if (command == "GET_LDR") {
        int valorLdr = ldrGetValue();
        Serial.printf("RES GET_LDR %d\n", valorLdr);
    }      
    
    else if (command.startsWith("SET_THRESHOLD ")) {
        String valorStr = command.substring(14);
        thresholdValue = valorStr.toInt(); 
        Serial.printf("RES SET_THRESHOLD 1\n");
    }
    else if (command == "GET_THRESHOLD") {
        Serial.printf("RES GET_THRESHOLD %d\n", thresholdValue);
    }      
}

// Função para atualizar o valor do LED
void ledUpdate(int valor) {
    // Valor deve convertar o valor recebido pelo comando SET_LED para 0 e 255
    // Normalize o valor do LED antes de enviar para a porta correspondente
    int valorConvertido = map(valor, 0, 100, 0, 255);
    analogWrite(ledPin, valorConvertido); 
}

// Função para ler o valor do LDR
int ldrGetValue() {
    // Leia o sensor LDR e retorne o valor normalizado entre 0 e 100
    // faça testes para encontrar o valor maximo do ldr (exemplo: aponte a lanterna do celular para o sensor)       
    // Atribua o valor para a variável ldrMax e utilize esse valor para a normalização
    int valorCru = analogRead(ldrPin);
    int valorNormalizado = map(valorCru, 0, ldrMax, 0, 100);
    
    if (valorNormalizado > 100) valorNormalizado = 100;
    if (valorNormalizado < 0) valorNormalizado = 0;
    
    return valorNormalizado;
}