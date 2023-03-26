import paho.mqtt.client as mqtt
import socket
from csv import writer

# Define the MQTT broker's IP address and port
broker_address = socket.gethostbyname('ece1894.eastus.cloudapp.azure.com')
broker_port = 1883

# Define the callback functions for different MQTT events
def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    # Subscribe to a topic upon connection
    client.subscribe("DryerTelemetry")

def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))
    appendToText(msg)

def appendToCSV():
    dataList=[]
    with open('event.csv', 'a') as f_object:
 
        # Pass this file object to csv.writer()
        # and get a writer object
        writer_object = writer(f_object)
    
        # Pass the list as an argument into
        # the writerow()
        writer_object.writerow(dataList)
    
        # Close the file object
        f_object.close()

def appendToText(msg):
    # Append-adds at last
    file1 = open("myfile.txt", "a")  # append mode
    file1.write(msg.payload)
    file1.close()

def createList():
    pass

# Create a new MQTT client instance
client = mqtt.Client()

# Set the callback functions for different MQTT events
client.on_connect = on_connect
client.on_message = on_message

# Connect to the MQTT broker
client.connect(broker_address, broker_port)

# Start the MQTT client's network loop
client.loop_start()

# Publish a message to the "test/topic" topic
# client.publish("test/topic", "Hello, world!")

# Wait for messages to be received
while True:
    pass

# Stop the MQTT client's network loop
client.loop_stop()
