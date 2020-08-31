package main

import (
	"bufio"
	"log"
	"net"
	"os"
	"runtime"
	"strconv"
	"time"
)

var response = []byte("HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!")

func hello(conn net.Conn) {
	reader := bufio.NewReader(conn)
	buf := make([]byte, 1024)

	for {
		to := time.Now().Add(5 * time.Second)
		if err := conn.SetReadDeadline(to); err != nil {
			log.Printf("Setting read deadline failed: %s", err)
			break
		}

		_, err := reader.Read(buf)
		if err != nil {
			log.Printf("Reading failed: %s", err)
			break
		}

		to = time.Now().Add(5 * time.Second)
		if err := conn.SetWriteDeadline(to); err != nil {
			log.Printf("Setting write deadline failed: %s", err)
			break
		}

		numWritten, err := conn.Write(response)
		if err != nil {
			log.Printf("Writing failed: %s", err)
			break
		}
		if numWritten != len(response) {
			log.Fatalln("Writing failed")
		}
	}

	conn.Close()
}

func main() {
	if len(os.Args) != 4 {
		log.Fatalf("Usage: %s <HOST-IPV4> <PORT> <GOMAXPROCS>", os.Args[0])
	}

	// TODO: Add some validation.
	host := os.Args[1]
	port := os.Args[2]

	maxProcs, err := strconv.Atoi(os.Args[3])
	if err != nil {
		log.Fatalf("Failed to parse max procs: %s", err)
	}

	runtime.GOMAXPROCS(maxProcs)

	l, err := net.Listen("tcp", host+":"+port)
	if err != nil {
		log.Fatalf("Listening failed: %s", err)
	}

	for {
		conn, err := l.Accept()
		if err != nil {
			log.Fatalf("Accepting failed: %s", err)
		}
		go hello(conn)
	}
}
