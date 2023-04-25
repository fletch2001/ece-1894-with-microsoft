import pandas
import numpy
from sklearn import linear_model

import paho.mqtt.client as mqtt
import socket

#importing data
data = pandas.read_csv("Book1.csv")
X = data[['Column2','Column3','Column4','Column5','Column6','Column7']]
dataLen = len(data.index)
y = numpy.arange(0,1,1/dataLen)

#creating a model object and fitting the model
regr = linear_model.LinearRegression()
regr.fit(X,y)


#prediction code
X_predict = [[-992.592041,195.56601,18.300001,1.19,-0.28,-1.68],[-998.082031,164.822006,-11.834001,1.47,0.28,-0.56], [2.074,-13.908,1010.892029,-0.7,2.17,-5.04]]
y_predict = regr.predict(X_predict)

print(y_predict)


#MQTT Receieve
# Define the MQTT broker's IP address and port
# broker_address = socket.gethostbyname('ece1894.eastus.cloudapp.azure.com')
broker_address = socket.gethostbyname('mqtt-dashboard.com')
broker_port = 1883

# Define the callback functions for different MQTT events
def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    # Subscribe to a topic upon connection
    client.subscribe("arf/DryerTelemetry")

def on_message(client, userdata, msg):
    # print(msg.topic+" "+str(msg.payload))
    listifiedData = listifyData(str(msg.payload))
    print(listifiedData)
    # print(predictClothes(listifiedData))
    client.publish("arf/microsoft/output", str(1.0-predictClothes(listifiedData)))

#==============================Parse Data and Predict===========================
def listifyData(message):
    inputData = message.split(",")
    # print(inputData)
    inputData.pop(0)
    inputData.pop(6)
    map(float, inputData)
    floats=[]
    floats = [list([float(x) for x in inputData])]
    # numpyData = numpy.array(inputDataRaw)
    # inputData = numpyData.astype(numpy.float64)
    # print(floats)
    # inputData=list(message)
    
    return floats

def predictClothes(X_predict):
    y_predict = regr.predict(X_predict)
    return y_predict
#===========================================================================


#====================================Start MQTT Stuff=================================
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
#================================End Start MQTT Stuff=================================