/***************************************************************************************
 *
 * @Copyright 2023, Inertial Sense Inc. <devteam@inertialsense.com>
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************************/

#pragma once

#include <rclcpp/rclcpp.hpp>

#include "ParamHelper.h"


class RtkBaseCorrectionProvider {
protected:
    ParamHelper ph_;

public:
    std::string type_;
    RtkBaseCorrectionProvider(rclcpp::Logger l, YAML::Node& node, const std::string& t) : ph_(node), type_(t), l_(l) { };
    virtual void configure(YAML::Node& node) = 0;
    rclcpp::Logger l_;
};

class RtkBaseCorrectionProvider_Ntrip : public RtkBaseCorrectionProvider {
public:
    std::string ip_ = "127.0.0.1";
    int port_ = 7777;
    std::string mount_point_;
    std::string username_;
    std::string password_;
    std::string credentials_;
    std::string authentication_;
    std::string protocol_; // format

    RtkBaseCorrectionProvider_Ntrip(rclcpp::Logger l, YAML::Node& node, std::string proto) : RtkBaseCorrectionProvider(l, node, "ntrip"), protocol_(proto) { configure(node); }
    virtual void configure(YAML::Node& node);
    void validate_credentials() { RCLCPP_WARN(l_, "NOT IMPLEMENTED"); }
    std::string get_connection_string();
};

class RtkBaseCorrectionProvider_Serial : public RtkBaseCorrectionProvider {
public:
    std::string port_ = "/dev/ttyACM0";
    int baud_rate_ = 115200;
    RtkBaseCorrectionProvider_Serial(rclcpp::Logger l, YAML::Node& node) : RtkBaseCorrectionProvider(l, node, "serial") { configure(node); }
    virtual void configure(YAML::Node& node);
};

class RtkBaseCorrectionProvider_ROS : public RtkBaseCorrectionProvider {
public:
    std::string topic_ = "/rtcm3_corrections";
    RtkBaseCorrectionProvider_ROS(rclcpp::Logger l, YAML::Node& node) : RtkBaseCorrectionProvider(l, node, "ros_topic"){ configure(node); }
    virtual void configure(YAML::Node& node);
};

class RtkBaseCorrectionProvider_EVB : public RtkBaseCorrectionProvider {
public:
    std::string port_ = "xbee";
    RtkBaseCorrectionProvider_EVB(rclcpp::Logger l, YAML::Node &node) : RtkBaseCorrectionProvider(l, node, "xbee_radio") { configure(node); }
    virtual void configure(YAML::Node& node);
};

class RtkBaseProvider {
protected:
    ParamHelper ph_;

public:

    typedef enum {
        OFF = 0,
        GPS1,
        GPS2,
    } base_gps_source;

    bool enable = true;     // enabled until explicitly disabled

    base_gps_source source_gps__serial0_ = OFF;
    base_gps_source source_gps__serial1_ = OFF;
    base_gps_source source_gps__serial2_ = OFF;
    base_gps_source source_gps__usb_ = OFF;

    std::string type_;
    std::string protocol_;

    bool compassing_enable_ = false;     // Enable RTK compassing (dual GNSS moving baseline RTK) at GPS2
    bool positioning_enable_ = false;    // Enable RTK precision positioning at GPS1

    std::vector<const RtkBaseCorrectionProvider *> correction_outputs_;

    RtkBaseProvider(rclcpp::Logger l, YAML::Node node) : ph_((YAML::Node &) node), l_(l) { configure( node); }

    void configure(YAML::Node &n);

    // FIXME:  Template me, so I can return the correct type...
    RtkBaseCorrectionProvider *getProvidersByType(std::string type);

    base_gps_source gpsSourceParamToEnum(YAML::Node &n, std::string v, std::string d = "off");
    rclcpp::Logger l_;
};

class RtkBaseCorrectionProviderFactory {
public:
    static RtkBaseCorrectionProvider* buildProvider(RtkBaseProvider& base, YAML::Node &node) {
        if (node.IsDefined() && !node.IsNull()) {
            std::string type = node["type"].as<std::string>();
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            if (type == "ntrip") return new RtkBaseCorrectionProvider_Ntrip(base.l_, node, base.protocol_);
            else if (type == "serial") return new RtkBaseCorrectionProvider_Serial(base.l_, node);
            else if (type == "ros_topic") return new RtkBaseCorrectionProvider_ROS(base.l_, node);
            else if (type == "evb") return new RtkBaseCorrectionProvider_EVB(base.l_, node);
        } else {
            RCLCPP_ERROR(base.l_, "Unable to configure RosBaseCorrectionProvider. The YAML node was null or undefined.");
        }
        return nullptr;
    }
};
