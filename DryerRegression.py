import pandas
import numpy
from sklearn import linear_model

data = pandas.read_csv("Book1.csv")

X = data[['Column2','Column3','Column4','Column5','Column6','Column7']]
dataLen = len(data.index)
y = numpy.arange(0,1,1/dataLen)

regr = linear_model.LinearRegression()
regr.fit(X,y)


#prediction code
X_predict = [[-992.592041,195.56601,18.300001,1.19,-0.28,-1.68],[-998.082031,164.822006,-11.834001,1.47,0.28,-0.56], [2.074,-13.908,1010.892029,-0.7,2.17,-5.04]]
y_predict = regr.predict(X_predict)

print(y_predict)
