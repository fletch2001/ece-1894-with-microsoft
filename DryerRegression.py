import pandas
import numpy
from sklearn import linear_model

data = pandas.read_csv("Book1.csv")

X = data[['Column2','Column3','Column4','Column5','Column6','Column7','Column8']]
dataLen = len(data.index)
y = numpy.arange(0,1,1/dataLen)

regr = linear_model.LinearRegression()
regr.fit(X,y)
