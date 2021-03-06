# 
# Created on: January 5, 2014
# Author: aousterh
#
# This script plots the average packet latency.
#
# Before running this script, generate the appropriate csv file
# using tcp_sender and tcp_receiver. The csv fill should be
# called data.csv
#
# This script can be run using:
# R < ./graph_avg_latency.R num_senders --save
#   num_senders - the number of clients. defaults to 1

# parameters of input data set
base_columns = 2
columns_per_sender = 5
start_packets_offset = 1
latency_sum_offset = 3

# read in data
data <- read.csv("data.csv", sep=",")

# read in arguments
args <- commandArgs()
num_senders = 1
if (length(args) > 2)
  num_senders <- as.integer(args[2])

# use the ggplot2 and reshape libraries
library('ggplot2')
library('reshape')

pdf(file="avg_latency.pdf", width=6.6, height=3)

# create a new data frame of only the data we want for this graph
interval_time = ((data$start_time + data$end_time) / 2 - data$start_time[1]) / 10^9
df = data.frame(interval_time)
for (i in 1:num_senders) {
    avg_packet_latency = data[, base_columns + (i-1) * columns_per_sender + latency_sum_offset] /
    (data[, base_columns + (i-1) * columns_per_sender + start_packets_offset] * 1000)
    df[length(df) + 1] <- avg_packet_latency
    colnames(df)[length(df)] <- paste('Client', i)
}
df2 <- na.omit(df)
new_data = melt(df2, id=1)

ggplot(new_data, aes(x=new_data$interval_time, y=new_data$value, color=new_data$variable)) + 
             geom_point() +
             labs(x = "Time (seconds)", y = "Average Latency (microseconds)") +
             scale_color_discrete(name = "Clients")


