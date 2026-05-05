#!/bin/bash 

PORT="/dev/ttyUSB0"
BAUD =1500000

echo "Connecting to the board on $PORT with baud rate $BAUD..."

if [ -c "$PORT" ]; then
    echo "Port $PORT found. Attempting to connect..."
    sudo screen $PORT $BAUD
    stty -F $PORT $BAUD
    cat $PORT
else
    echo "Error: Port $PORT not found. Please check the connection and try again."
fi