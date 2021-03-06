#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Constants used by the path planner
#define MAX_POINTS 50           	// Maximum number of points available to the path planner
#define TARGET_SPEED 49.5			// Speed limit
#define LANE_WIDTH 4				// Lane width in meters
#define LANE_CENTER (LANE_WIDTH / 2)// Center of the lane in meters
#define LANE_CHANGE_THRESHOLD 25	// Distance between car in front that will trigger a lane change
#define LANE_CHANGE_DIST_FRONT 30	// Required distance between car in front to allow safe lane change
#define LANE_CHANGE_DIST_REAR 15	// Required distance between car behind to allow safe lane change
#define MOVEMENT_DT 0.02			// Movement every 0.2ms
#define ACCEL_DELTA .3  			// Acceleration/deceleration +/- 5m/s^2
#define POINT_SPACING 30            // Spacing in meters between path points
#define PATH_NUMPOINTS 3            // Number of evenly spaced points to add
#define MILESHOUR_METERSECOND 2.24  // Convert miles/hour to meters/second
#define LANE_TWO 2                  // Rightmost lane
#define LANE_ONE  1                 // Center lane
#define LANE_ZERO 0                 // Leftmost lane

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s)
{
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}
double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, vector<double> maps_x, vector<double> maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}
int NextWaypoint(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}
// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}
// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, vector<double> maps_s, vector<double> maps_x, vector<double> maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}
// Check if a lane change to the target lane can be performed safely without hitting a car already in that lane
bool safe_lane_change(vector<vector<double>> sensor_fusion, int prev_path_size, double car_s, int target_lane, double ref_vel)
{
	bool lane_change_safe_flag = true;

    // Loop through all detected cars
	for (int j = 0; j < sensor_fusion.size(); j++) {
		float d = sensor_fusion[j][6];                      // lane of the detected car

		// Only check detected cars that are currently in the target lane (i.e. lane to our left)
		if (d < (LANE_CENTER + LANE_WIDTH * target_lane + LANE_CENTER) &&
			d > (LANE_CENTER + LANE_WIDTH * target_lane - LANE_CENTER)) {

			// Determine the car's current speed and position
			double vx = sensor_fusion[j][3];
			double vy = sensor_fusion[j][4];
			double detected_car_speed = sqrt(vx * vx + vy * vy);
			double detected_car_s = sensor_fusion[j][5];

			// Determine the car's future position (assuming it is not currently changing lanes, which may be an
            // incorrect assumption (and thus a weakness of this approach)
			detected_car_s += (double) prev_path_size * MOVEMENT_DT * detected_car_speed;

			// Leave enough room in front and behind us after the lane change. If that is not possible
			// do not change lanes
			if ((detected_car_s > car_s) && ((detected_car_s - car_s) < LANE_CHANGE_DIST_FRONT))
            {
                // The detected car is IN FRONT of us (in the target lane). Do not change lanes if the detected car
                // would end up too close in front of us. Also do not change lanes if the car in the target lane is
                // going slower than our car to avoid our car having to change lanes again immediately in order to
                // avoid changing lanes into the slower car
                if (detected_car_speed < ref_vel) {
                    lane_change_safe_flag = false;
                }
            }

            if ((detected_car_s < car_s) && ((car_s - detected_car_s) < LANE_CHANGE_DIST_REAR))
            {
				// Do not change lanes if the detected car would end up too close BEHIND us to avoid cutting cars off
                // and possibly running into cars going faster than us
				lane_change_safe_flag = false;
			}
		}
	}

	return lane_change_safe_flag;
}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

	ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

	// Lane and speed of the car at the start of the simulation
	int lane = LANE_ONE; 		// The current lane, assume the car starts in lane 1 (center lane)
    double ref_vel = 0.0;		// Start the car from standstill

	h.onMessage([&ref_vel, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy, &lane](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];

          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

            // Begin collision detection

            // The previous path being followed by the car
			int prev_path_size = previous_path_x.size();

            if (prev_path_size>0) {
                car_s = end_path_s;
            }

            // Reset variables every cycle
            bool too_close = false;
            int to_left = lane - 1;
            int to_right = lane + 1;
			bool lane_change_safe = false;
			int target_lane = lane;

            // Loop through all detected cars
            for(int i=0;i<sensor_fusion.size(); i++)
            {
                float d = sensor_fusion[i][6];

                // The detected car is in the same lane as our car, ignore the car if it is not
                if (d < (LANE_CENTER + LANE_WIDTH * lane + LANE_CENTER) &&
                    d > (LANE_CENTER + LANE_WIDTH * lane - LANE_CENTER))
                {
                    // Determine speed and position
                    double vx = sensor_fusion[i][3];                    // detected car's speed x direction
                    double vy = sensor_fusion[i][4];                    // detected car's speed y direction
                    double detected_car_speed = sqrt(vx*vx + vy*vy);    // speed vector magnitude
                    double detected_car_s = sensor_fusion[i][5];        // s value of the detected car

                    // Predict the future s value of the detected car (20ms from now)
                    detected_car_s += ((double)prev_path_size * MOVEMENT_DT * detected_car_speed);

                    // If the detected car will be too close to us evasive action is required
                    if((detected_car_s > car_s) && (detected_car_s - car_s) < LANE_CHANGE_THRESHOLD)
                    {
						too_close = true;

						// Try changing one lane to the left (only if we are not already in the left most lane)
                        if (to_left >= LANE_ZERO) {
                            lane_change_safe = true;
							lane_change_safe = safe_lane_change(sensor_fusion, prev_path_size, car_s, to_left, ref_vel);
							if (lane_change_safe)
								target_lane = to_left;
                        }

                        // If a lane change to the left is not possible try a lane change to the right (only if we
                        // are not already in the rightmost lane)
                        if ((lane_change_safe == false) && (to_right <= LANE_TWO)) {
							lane_change_safe = true;
                            lane_change_safe = safe_lane_change(sensor_fusion, prev_path_size, car_s, to_right, ref_vel);
							if (lane_change_safe)
								target_lane = to_right;
						}
                    }
                }
            }

			// Effectuate any safe lane change and required acceleration/deceleration
			if (lane_change_safe) {
                cout << "change from " << lane << " to " << target_lane << endl;
				lane = target_lane;
			}
            else if (too_close)
            {
                // Else if a lane change is not possible and the car in front is too close, slow down
                ref_vel -= ACCEL_DELTA;
            }
            else if(ref_vel < TARGET_SPEED)
            {
                // The current lane is clear so accelerate until the speed limit is reached
                ref_vel += ACCEL_DELTA;
            }

            // End collision detection

            // Begin create new path code
			vector<double> ptsx;
			vector<double> ptsy;

			double ref_x = car_x;
			double ref_y = car_y;
			double ref_yaw = deg2rad(car_yaw);

			// If the previous path is almost empty use the car's current position as the starting point
			if (prev_path_size < 2)
			{
				ptsx.push_back(car_x - cos(car_yaw));
				ptsx.push_back(car_x);
				ptsy.push_back(car_y - sin(car_yaw));
				ptsy.push_back(car_y);
			}
			else
			{	// Else use the previous path's last two points as the starting point
				ref_x = previous_path_x[prev_path_size-1];
				ref_y = previous_path_y[prev_path_size-1];

				double ref_x_prev = previous_path_x[prev_path_size-2];
				double ref_y_prev = previous_path_y[prev_path_size-2];
				ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);

                // Use the two points that make the path tangent to the previous path's two end points
				ptsx.push_back(ref_x_prev);
				ptsx.push_back(ref_x);
				ptsy.push_back(ref_y_prev);
				ptsy.push_back(ref_y);
			}

            // Add evenly spaced points to the starting point
            for (int i=1; i <= PATH_NUMPOINTS; i++) {
                // get x and y coordinates based on transformations of {s, d} coordinates.
                vector<double> xy_temp = getXY(car_s + POINT_SPACING * i, (LANE_CENTER + LANE_WIDTH * lane),
                                            map_waypoints_s, map_waypoints_x, map_waypoints_y);

                ptsx.push_back(xy_temp[0]);
                ptsy.push_back(xy_temp[1]);
            }

            // ptsx and ptsy now hold the last two points plus PATH_NUMPOINTS evenly spaced points in GLOBAL coordinates

            // Convert the ptsx and ptsy vectors from global to local car coordinates. Shift and rotate car reference
            // angle to zero degrees to make the math easier
			for (int i = 0; i < ptsx.size(); i++)
			{
				double shift_x = ptsx[i] - ref_x;
				double shift_y = ptsy[i] - ref_y;

				ptsx[i] = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
				ptsy[i] = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));
			}

            // ptsx and ptsy now hold the last two points plus PATH_NUMPOINTS evenly spaced points in LOCAL coordinates

            // Points to be used by the path planner
            vector<double> next_x_vals;
            vector<double> next_y_vals;

            // Create a spline and set its x and y points
			tk::spline s;
			s.set_points(ptsx, ptsy);

            // Start the new path with all the previous path points that have not yet been used
			for(int i = 0;i<previous_path_x.size();i++)
			{
				next_x_vals.push_back(previous_path_x[i]);
				next_y_vals.push_back(previous_path_y[i]);
			}

            // Calculate how to break up spline points to travel at the desired speed limit
			double target_x = POINT_SPACING;
			double target_y = s(target_x);  // ask the spline for the y coordinate
			double target_dist = sqrt((target_x)*(target_x) + (target_y)*(target_y));
			double x_add_on = 0;            // origin

            // N is the number of points along the lane converted from miles/hour to m/s
            double N = (target_dist / (MOVEMENT_DT * ref_vel/MILESHOUR_METERSECOND));

            // Add path planner points until MAX_POINTS points are ready
			for(int i = 1;i <= MAX_POINTS - previous_path_x.size(); i++){
				double x_point = x_add_on + target_x / N;
				double y_point = s(x_point);        // ask the spline for the y coordinate

				x_add_on = x_point;
                double x_ref = x_point;
				double y_ref = y_point;

                // Rotate and shift back to global coordinates
				x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw)) + ref_x;
				y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw)) + ref_y;

                // Add the points to the new path and continue with the next
				next_x_vals.push_back(x_point);
				next_y_vals.push_back(y_point);
			}
            // End create new path code

			msgJson["next_x"] = next_x_vals;
			msgJson["next_y"] = next_y_vals;

			auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
















































































