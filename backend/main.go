package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

type JoystickInput struct {
	Direction string `json:"direction"`
}

type GameState struct {
	PlayerX        int     `json:"playerX"`
	Direction      string  `json:"direction"`
	Score          int     `json:"score"`
	Running        bool    `json:"running"`
	ElapsedSeconds int     `json:"elapsedSeconds"`
	ElapsedTime    string  `json:"elapsedTime"`
	Acceleration   float64 `json:"acceleration"`
	PlayerSpeed    int     `json:"playerSpeed"`
}

var (
	mutex sync.Mutex

	playerX   = 225
	direction = "center"
	score     = 0
	running   = false

	elapsedSeconds = 0
	acceleration   = 1.0

	screenWidth = 500
	playerWidth = 50

	// Velocidade base do carro
	basePlayerSpeed = 20

	// Velocidade atual do carro
	playerSpeed = 20

	// Timer do jogo
	gameTickerStop chan bool

	clients = make(map[*websocket.Conn]bool)
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func main() {
	http.HandleFunc("/joystick", corsMiddleware(handleJoystick))
	http.HandleFunc("/game/start", corsMiddleware(handleGameStart))
	http.HandleFunc("/game/stop", corsMiddleware(handleGameStop))
	http.HandleFunc("/game/status", corsMiddleware(handleGameStatus))
	http.HandleFunc("/ws", handleWebSocket)

	log.Println("Servidor rodando em http://localhost:8080")
	log.Println("Endpoint joystick: POST http://localhost:8080/joystick")
	log.Println("Endpoint start:    POST http://localhost:8080/game/start")
	log.Println("Endpoint stop:     POST http://localhost:8080/game/stop")
	log.Println("Endpoint status:   GET  http://localhost:8080/game/status")
	log.Println("WebSocket:         ws://localhost:8080/ws")

	if err := http.ListenAndServe(":8080", nil); err != nil {
		log.Fatal("erro ao iniciar servidor:", err)
	}
}

func corsMiddleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")

		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusOK)
			return
		}

		next(w, r)
	}
}

func handleJoystick(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "método não permitido", http.StatusMethodNotAllowed)
		return
	}

	var input JoystickInput

	if err := json.NewDecoder(r.Body).Decode(&input); err != nil {
		http.Error(w, "json inválido", http.StatusBadRequest)
		return
	}

	mutex.Lock()

	if !running {
		state := buildGameStateLocked()
		mutex.Unlock()

		writeJSON(w, state)
		return
	}

	switch input.Direction {
	case "left":
		playerX -= playerSpeed
		direction = "left"

	case "right":
		playerX += playerSpeed
		direction = "right"

	case "center":
		direction = "center"

	default:
		mutex.Unlock()
		http.Error(w, "direção inválida. Use: left, right ou center", http.StatusBadRequest)
		return
	}

	limitPlayerPositionLocked()

	state := buildGameStateLocked()

	mutex.Unlock()

	log.Printf(
		"Joystick recebido: direction=%s playerX=%d speed=%d acceleration=%.1fx time=%s\n",
		state.Direction,
		state.PlayerX,
		state.PlayerSpeed,
		state.Acceleration,
		state.ElapsedTime,
	)

	broadcastGameState(state)

	writeJSON(w, state)
}

func handleGameStart(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "método não permitido", http.StatusMethodNotAllowed)
		return
	}

	stopGameTicker()

	mutex.Lock()

	playerX = 225
	direction = "center"
	score = 0
	running = true

	elapsedSeconds = 0
	acceleration = 1.0
	playerSpeed = basePlayerSpeed

	state := buildGameStateLocked()

	mutex.Unlock()

	startGameTicker()

	log.Println("Jogo iniciado")

	broadcastGameState(state)

	writeJSON(w, map[string]any{
		"message": "jogo iniciado",
		"state":   state,
	})
}

func handleGameStop(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "método não permitido", http.StatusMethodNotAllowed)
		return
	}

	stopGameTicker()

	mutex.Lock()

	running = false
	direction = "center"

	state := buildGameStateLocked()

	mutex.Unlock()

	log.Println("Jogo parado")

	broadcastGameState(state)

	writeJSON(w, map[string]any{
		"message": "jogo parado",
		"state":   state,
	})
}

func handleGameStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "método não permitido", http.StatusMethodNotAllowed)
		return
	}

	mutex.Lock()
	state := buildGameStateLocked()
	mutex.Unlock()

	writeJSON(w, state)
}

func handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println("erro ao abrir websocket:", err)
		return
	}

	mutex.Lock()

	clients[conn] = true
	state := buildGameStateLocked()

	mutex.Unlock()

	log.Println("Frontend conectado via WebSocket")

	if err := conn.WriteJSON(state); err != nil {
		log.Println("erro ao enviar estado inicial:", err)

		mutex.Lock()
		delete(clients, conn)
		mutex.Unlock()

		conn.Close()
		return
	}

	for {
		_, _, err := conn.ReadMessage()
		if err != nil {
			mutex.Lock()
			delete(clients, conn)
			mutex.Unlock()

			conn.Close()

			log.Println("Frontend desconectado")
			break
		}
	}
}

func startGameTicker() {
	gameTickerStop = make(chan bool)

	go func() {
		ticker := time.NewTicker(1 * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-ticker.C:
				mutex.Lock()

				if !running {
					mutex.Unlock()
					continue
				}

				elapsedSeconds++

				// Score sobe com o tempo
				score++

				// A cada 10 segundos aumenta a aceleração em 0.2
				acceleration = 1.0 + float64(elapsedSeconds/10)*0.2

				// Limite de aceleração para não ficar impossível rápido demais
				if acceleration > 3.0 {
					acceleration = 3.0
				}

				// Velocidade do carro também aumenta com a aceleração
				playerSpeed = int(float64(basePlayerSpeed) * acceleration)

				state := buildGameStateLocked()

				mutex.Unlock()

				broadcastGameState(state)

			case <-gameTickerStop:
				return
			}
		}
	}()
}

func stopGameTicker() {
	if gameTickerStop != nil {
		select {
		case gameTickerStop <- true:
		default:
		}

		gameTickerStop = nil
	}
}

func buildGameStateLocked() GameState {
	return GameState{
		PlayerX:        playerX,
		Direction:      direction,
		Score:          score,
		Running:        running,
		ElapsedSeconds: elapsedSeconds,
		ElapsedTime:    formatElapsedTime(elapsedSeconds),
		Acceleration:   acceleration,
		PlayerSpeed:    playerSpeed,
	}
}

func limitPlayerPositionLocked() {
	minX := 0
	maxX := screenWidth - playerWidth

	if playerX < minX {
		playerX = minX
	}

	if playerX > maxX {
		playerX = maxX
	}
}

func formatElapsedTime(totalSeconds int) string {
	minutes := totalSeconds / 60
	seconds := totalSeconds % 60

	return fmt.Sprintf("%02d:%02d", minutes, seconds)
}

func broadcastGameState(state GameState) {
	mutex.Lock()

	clientList := make([]*websocket.Conn, 0, len(clients))

	for client := range clients {
		clientList = append(clientList, client)
	}

	mutex.Unlock()

	for _, client := range clientList {
		if err := client.WriteJSON(state); err != nil {
			log.Println("erro ao enviar websocket:", err)

			client.Close()

			mutex.Lock()
			delete(clients, client)
			mutex.Unlock()
		}
	}
}

func writeJSON(w http.ResponseWriter, data any) {
	w.Header().Set("Content-Type", "application/json")

	if err := json.NewEncoder(w).Encode(data); err != nil {
		log.Println("erro ao escrever json:", err)
	}
}
