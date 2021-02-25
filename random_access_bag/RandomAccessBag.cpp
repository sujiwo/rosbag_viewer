/*
 *  Copyright (c) 2019, Nagoya University
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *  THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * RandomAccessBag.cpp
 *
 *  Created on: Aug 15, 2018
 *      Author: sujiwo
 */

#include <algorithm>
#include <exception>
#include <rosbag/query.h>
#include <stdexcept>

#include "RandomAccessBag.h"

//#include "access_private.hpp"

using namespace std;

/*
ACCESS_PRIVATE_FIELD(rosbag::MessageInstance, rosbag::IndexEntry const,
                     index_entry_);
*/

/*
 * A Notes about ROS Bags:
 */

/*
 * Only allows one single topic
 */
RandomAccessBag::RandomAccessBag(const rosbag::Bag &_bag,
                                 const std::string &topic)
    :

      bagstore(_bag),
      viewTopic(topic), rosbag::View::View(_bag, rosbag::TopicQuery(topic))

{
  bagStartTime = getBeginTime();
  bagStopTime = getEndTime();

  createCache();
}

RandomAccessBag::RandomAccessBag(const rosbag::Bag &_bag,
                                 const std::string &topic, const ros::Time &t1,
                                 const ros::Time &t2)
    :

      bagstore(_bag),
      viewTopic(topic), rosbag::View::View(_bag)

{
  bagStartTime = getBeginTime();
  bagStopTime = getEndTime();

  ros::Time t1x, t2x;
  t1x = (t1 == ros::TIME_MIN ? bagStartTime : t1);
  t2x = (t2 == ros::TIME_MAX ? bagStopTime : t2);

  setTimeConstraint(t1x, t2x);
}

RandomAccessBag::RandomAccessBag(rosbag::Bag const &_bag,
                                 const std::string &topic,
                                 const double seconds1, const double seconds2)
    :

      bagstore(_bag),
      viewTopic(topic), rosbag::View::View(_bag)

{
  bagStartTime = getBeginTime();
  bagStopTime = getEndTime();

  setTimeConstraint(seconds1, seconds2);
}

RandomAccessBag::~RandomAccessBag() {}

void RandomAccessBag::createCache() {
  update();
  rosbag::View::size();
  iterator it = begin();
  size_t sz = this->size();
  conn = getConnections()[0];
  msgPtr.clear();
  msgPtr.reserve(sz);

  for (uint32_t p = 0; p < sz; p++) {
    rosbag::MessageInstance m = *it;
    // XXX: Fix this !
//    rosbag::IndexEntry const ie = access_private::index_entry_(m);
    msgPtr.push_back(m);
    ++it;
  }
}


void RandomAccessBag::setTimeConstraint(double seconds1, double seconds2)
{
	ros::Time t1, t2;
	if (seconds1==-1)
		seconds1=0;
	ros::Duration td1(seconds1);
	t1 = bagStartTime + td1;
	if (seconds2==-1)
		t2 = bagStopTime;
	else {
		ros::Duration td2(seconds2);
		t2 = bagStartTime+td2;
	}
	assert(t1 < t2);

	setTimeConstraint(t1, t2);
}


void RandomAccessBag::setTimeConstraint(const ros::Time &t1, const ros::Time &t2)
{
	if (t1 < bagStartTime or t1 > bagStopTime)
		throw out_of_range("Requested start time is out of range");

	if (t2 < bagStartTime or t2 > bagStopTime)
		throw out_of_range("Requested stop time is out of range");

	originalZeroIndex = getPositionAtTime(t1);

	queries_.clear();
	ranges_.clear();
	addQuery(bagstore, rosbag::TopicQuery(viewTopic), t1, t2);
	createCache();
	bagStartTime = getBeginTime();
	bagStopTime = getEndTime();
	mIsTimeConstrained = true;
}

void RandomAccessBag::resetTimeConstraint() {
  queries_.clear();
  ranges_.clear();
  addQuery(bagstore, rosbag::TopicQuery(viewTopic));
  createCache();
  mIsTimeConstrained = false;
}

uint32_t RandomAccessBag::getPositionAtDurationSecond(const double S) const {
  ros::Duration Sd(S);
  ros::Time Tx = msgPtr.at(0).getTime() + Sd;
  if (Tx < msgPtr.at(0).getTime() or Tx > msgPtr.back().getTime())
    throw out_of_range("Requested time is out of bag");

  return getPositionAtTime(Tx);
}

uint32_t RandomAccessBag::getPositionAtTime(const ros::Time &tx) const {
  ros::Time txi;
  const ros::Time bstart = msgPtr.front().getTime(), bstop = msgPtr.back().getTime();

  // Check whether tx is inside the range, with tolerance of 1 microsecond
  if (tx < bstart) {
    if (abs((tx - bstart).toNSec()) > 1000)
      throw std::out_of_range("Requested time is below the range");
    else
      txi = bstart;
  }

  else if (tx > bstop) {
    if (abs((tx - bstop).toNSec()) > 1000)
      throw std::out_of_range("Requested time is over the range");
    else
      txi = bstop;
  }

  else
    txi = tx;

  auto it =
      std::lower_bound(msgPtr.begin(), msgPtr.end(), txi,
                       [](const rosbag::MessageInstance &iptr, const ros::Time &t) {
                         return (iptr.getTime() < t);
                       });

  // check if either solution p or p-1 is closer to our requested time
  uint32_t p = it - msgPtr.begin();
  if (p > 0 and p < size() - 1) {
    auto td1 = msgPtr.at(p).getTime() - tx, td0 = txi - msgPtr.at(p - 1).getTime();
    return (td1 > td0 ? p - 1 : p);
  } else
    return p;
}

/*
 * Convert time as represented by seconds from offset
 */
ros::Time RandomAccessBag::timeFromOffset(const double secondsFromStart) const {
  assert(secondsFromStart >= 0 and secondsFromStart <= length().toSec());

  ros::Duration td(secondsFromStart);
  return msgPtr.at(0).getTime() + td;
}

/*
 * Convert time as represented by seconds from start of bag
 */
ros::Time RandomAccessBag::timeFromStart(const double seconds) const {
  assert(seconds >= 0 and seconds <= (bagStopTime - bagStartTime).toSec());

  ros::Duration td(seconds);
  return bagStartTime + td;
}

typedef std::map<uint32_t, rosbag::ConnectionInfo *> connectionListT;
//ACCESS_PRIVATE_FIELD(rosbag::Bag, connectionListT, connections_);

/*
 * Returns list of topics and their message types
 * topic -> message type
 */
std::map<std::string, std::string>
RandomAccessBag::getTopicList(const rosbag::Bag &bag) {
  std::map<std::string, std::string> topicList;

  rosbag::View nview(bag);
  vector<const rosbag::ConnectionInfo*> conList = nview.getConnections();
  for (auto &conn: conList) {
	  topicList.insert(make_pair(conn->topic, conn->datatype));
  }

  return topicList;

}

std::string RandomAccessBag::messageType() const { return conn->datatype; }

void
RandomAccessBag::desample(const float newFreq, DesampledMessageList &messagePosList) const
{
	// Must be lower than current frequency
	assert(newFreq < hz());

	messagePosList.clear();

	const double lengthInSeconds = (getBagStopTime()-getBagStartTime()).toSec();

	uint posWk = 0, nextWk;
	const double tIntrv = 1.0 / newFreq;
	for (double twork=0.0; twork<lengthInSeconds; twork+=1.0) {
		double tMax = min(twork+1.0, lengthInSeconds);
		double tm = twork+tIntrv;
		while (tm < tMax) {
			uint p = getPositionAtDurationSecond(tm);
			messagePosList.push_back(p);
			tm += tIntrv;
		}
	}

}
