#!/usr/bin/python

import rospy
import rosbag


class SyncPlayer():
    def __init__ (self, bagPath):
        self.topicMap = {}
        self.topicHandler = {}
        self.bag = rosbag.Bag(bagPath, mode="r")
        
    def addTopicMapping(self, topicFrom, topicTo):
        try:
            self.topicMap[topicFrom].append(topicTo)
            pass
        except KeyError:
            self.topicMap[topicFrom] = [topicTo]

    def play(self):
        pass


if __name__=="__main__":
    
    player = SyncPlayer("/media/sujiwo/ssd/log_short.bag")
    player.addTopicMapping("/camera1/image_raw", "/camera1/image_proc")
    
    pass