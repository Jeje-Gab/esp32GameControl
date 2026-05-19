package main

import (
	"encoding/json"
	"log"
	"net/http"
	"sync"

	"github.com/gorilla/websocket"
)

type JoystickInput struct {
	Direction string `json:"direction"`
}

type GameState struct {
	PlayerX   int    `json:"playerX"`
	Direction string `json:"direction"`
	Score     int    `json:"score"`
	Running   bool   `json:"running"`
}

var (
	mutex sync.Mutex

	playerX = 225
	score   = 0
	running = false

	screenWidth = 500
	playerWidth = 50
	playerSpeed = 10

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
	http.HandleFunc("/game/status", corsMiddleware(handleGameStatus))
	http.HandleFunc("/ws", handleWebSocket)

	log.Println("Servidor rodando em http://localhost:8080")
	log.Println("Endpoint joystick: POST http://localhost:8080/joystick")
	log.Println("Endpoint start:    POST http://localhost:8080/game/start")
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
		state := GameState{
			PlayerX:   playerX,
			Direction: "center",
			Score:     score,
			Running:   running,
		}

		mutex.Unlock()

		writeJSON(w, state)
		return
	}

	switch input.Direction {
	case "left":
		playerX -= playerSpeed
	case "right":
		playerX += playerSpeed
	case "center":
		// parado
	default:
		mutex.Unlock()
		http.Error(w, "direção inválida. Use: left, right ou center", http.StatusBadRequest)
		return
	}

	minX := 0
	maxX := screenWidth - playerWidth

	if playerX < minX {
		playerX = minX
	}

	if playerX > maxX {
		playerX = maxX
	}

	state := GameState{
		PlayerX:   playerX,
		Direction: input.Direction,
		Score:     score,
		Running:   running,
	}

	mutex.Unlock()

	log.Printf("Joystick recebido: direction=%s playerX=%d\n", input.Direction, state.PlayerX)

	broadcastGameState(state)

	writeJSON(w, state)
}

func handleGameStart(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "método não permitido", http.StatusMethodNotAllowed)
		return
	}

	mutex.Lock()

	playerX = 225
	score = 0
	running = true

	state := GameState{
		PlayerX:   playerX,
		Direction: "center",
		Score:     score,
		Running:   running,
	}

	mutex.Unlock()

	log.Println("Jogo iniciado")

	broadcastGameState(state)

	writeJSON(w, map[string]any{
		"message": "jogo iniciado",
		"state":   state,
	})
}

func handleGameStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "método não permitido", http.StatusMethodNotAllowed)
		return
	}

	mutex.Lock()

	state := GameState{
		PlayerX:   playerX,
		Direction: "center",
		Score:     score,
		Running:   running,
	}

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

	state := GameState{
		PlayerX:   playerX,
		Direction: "center",
		Score:     score,
		Running:   running,
	}
	mutex.Unlock()

	log.Println("Frontend conectado via WebSocket")

	if err := conn.WriteJSON(state); err != nil {
		log.Println("erro ao enviar estado inicial:", err)
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

func broadcastGameState(state GameState) {
	mutex.Lock()
	defer mutex.Unlock()

	for client := range clients {
		err := client.WriteJSON(state)
		if err != nil {
			log.Println("erro ao enviar websocket:", err)

			client.Close()
			delete(clients, client)
		}
	}
}

func writeJSON(w http.ResponseWriter, data any) {
	w.Header().Set("Content-Type", "application/json")

	if err := json.NewEncoder(w).Encode(data); err != nil {
		log.Println("erro ao escrever json:", err)
	}
}
