# Project 1: CarND-Path-Planning-Project
Self-Driving Car Engineer Nanodegree Program

## Goals
The goal is to safely navigate a car around a virtual highway with other traffic that is driving +/-10 MPH of the 50 MPH speed limit. In addition to the car's localization and sensor fusion data, there is also a sparse map list of waypoints around the highway. The car tries to go as close as possible to the 50 MPH speed limit, passing slower traffic when possible. The car avoids hitting other cars and drives inside of the marked road lanes at all times, except when changing lanes. The car does not experience total acceleration over 10 m/s^2 and jerk that is greater than 50 m/s^3.


## Basic Build Instructions

1. Clone this repo.
2. Make a build directory: `mkdir build && cd build`
3. Compile: `cmake .. && make`
4. Run it: `./path_planning`.

## Creating the Path
To create the path to follow the approach using splines described in the walkthrough is used. It essentially uses `MAX_POINTS` (50 in our case) waypoints starting at the end of the previous path (or, at the start of the simulation, with the car's current position). Then a number of additional (`PATH_NUMPOINTS`, or 3 in our case) waypoints spaced evenly apart (`POINT_SPACING` or 30m in our case) are added. A spline is then fitted to determine intermediate points and produce a smooth vector of waypoints for the car to follow.

## Avoiding Collisions
The approach to avoiding collisions with cars driving in the same lane is to:

1. Look for cars travelling in the same lane that are deemed too close to our car (set using parameter `LANE_CHANGE_THRESHOLD`)
2. If our car gets too close to another car it tries changing lanes to the left. First it checks if there is a car in the lane to our left, allowing for additional room in front (using `LANE_CHANGE_DIST_FRONT`) and behind (using `LANE_CHANGE_DIST_REAR`) of our car to prevent changing lanes and ending up too close to the other car
3. If a lane change to the left is not possible because a car in that lane would end up too close to our car, or if our car is already in the leftmost lane, a lane change to the right is attempted using the same approach
4. If a lane change to the right is also not possible, the only other option is to slow down and stay in the current lane.

If there are no cars in front of our car, our car will always keep accelerating until it reaches the speed limit.

Three constants are used to determine when a lane change is required, and how much room there needs to be between our car and cars already in the other lane. Theses variables are `LANE_CHANGE_THRESHOLD`, `LANE_CHANGE_DIST_FRONT` and `LANE_CHANGE_DIST_REAR`. Their values were found using trial and error, observing lane change behaviour in the simulator and finding a balance between aggressively changing lanes and maintaining the maximum speed, while avoiding changing lanes into or cutting off other cars.  

## Initial State
The assumption is that our car will always start in the center lane. It will also start from standstill and gradually accelerate to avoid exceeding the maximum acceleration limit.

## Possible Improvements
1. The car currently favours lane changes to the left because that is the first lane change it will consider when getting too close to another car in the same lane. Only if a lane change to the left is not possible a lane change to the right is considered
2. A lane change only checks for cars immediately to our left or right, with a little bit of space added for safety. It does not consider other traffic in that lane. E.g. it may decide to change lanes to a lane with lots of traffic, or slower traffic, instead of a lane with no other traffic
3. Our car does not check for any cars changing lanes into our lane. Occassionally another car will cause a collision because it is changing lanes into our lane.