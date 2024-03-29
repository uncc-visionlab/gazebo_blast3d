/*
 * Copyright (C) 2012-2015 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "gazebo_blast3d_microphone_plugin.h"

#include <math.h>
#include <string>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <vector>

using namespace std;

namespace gazebo {

    GZ_REGISTER_MODEL_PLUGIN(GazeboBlast3DMicrophonePlugin)

    /////////////////////////////////////////////////
    GazeboBlast3DMicrophonePlugin::GazeboBlast3DMicrophonePlugin() :
    frame_id_(kDefaultFrameId), link_name_(kDefaultLinkName), ModelPlugin(),
            background_audio_index_(0), explosion_triggered_(false) {

    }

    /////////////////////////////////////////////////

    GazeboBlast3DMicrophonePlugin::~GazeboBlast3DMicrophonePlugin() {
        updateConnection_->~Connection();
    }

    /////////////////////////////////////////////////

    void GazeboBlast3DMicrophonePlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf) {
        if (!_model)
            gzerr << "Invalid sensor pointer.\n";
        if (kPrintOnPluginLoad) {
            gzdbg << __FUNCTION__ << "() called." << std::endl;
        }

        // Store the pointer to the model and the world.
        model_ = _model;
        world_ = model_->GetWorld();

        //==============================================//
        //========== READ IN PARAMS FROM SDF ===========//
        //==============================================//

        // Use the robot namespace to create the node handle.
        if (_sdf->HasElement("robotNamespace"))
            namespace_ = _sdf->GetElement("robotNamespace")->Get<std::string>();
        else
            gzerr << "[gazebo_microphone_plugin] Please specify a robotNamespace.\n";

        // Get node handle.
        node_handle_ = transport::NodePtr(new transport::Node());

        // Initialize with default namespace (typically /gazebo/default/).
        node_handle_->Init(namespace_);

        if (_sdf->HasElement("linkName"))
            link_name_ = _sdf->GetElement("linkName")->Get<std::string>();
        else
            gzerr << "[gazebo_microphone_plugin] Please specify a linkName.\n";

        // Get the pointer to the link.
        link_ = model_->GetLink(link_name_);
        if (link_ == NULL)
            gzthrow("[gazebo_microphone_plugin] Couldn't find specified link \"" << link_name_ << "\".");

        frame_id_ = link_name_;

        node_handle_ = transport::NodePtr(new transport::Node());
        node_handle_->Init(namespace_);

        getSdfParam<std::string>(_sdf, "audioLinkTopic", blast3d_audio_topic_,
                blast3d_audio_topic_);
        getSdfParam<std::string>(_sdf, "customAudioDataFolder", blast3d_audio_datafolder_,
                blast3d_audio_datafolder_);
        // READ THE AUDIO FILE FOR BACKGROUND
        std::string background_file = blast3d_audio_datafolder_ + "/background_loop.wav";
        // READ THE AUDIO FILE FOR BOOM
        std::string bomb_file = blast3d_audio_datafolder_ + "/bomb.wav";
        
//        bool load (std::string filePath);
        LoadAudioFiles(background_file, bomb_file);
        
        audio_pub_ = node_handle_->Advertise<sensor_msgs::msgs::Audio>(blast3d_audio_topic_, 10);
        updateConnection_ = event::Events::ConnectWorldUpdateBegin(
                boost::bind(&GazeboBlast3DMicrophonePlugin::OnUpdate, this, _1));
    }

    /////////////////////////////////////////////////

    void GazeboBlast3DMicrophonePlugin::OnUpdate(const common::UpdateInfo& _info) {
    
        // Get the current simulation time.
#if GAZEBO_MAJOR_VERSION >= 9
        common::Time now = world_->SimTime();
#else
        common::Time now = world_->GetSimTime();
#endif
        // COMPUTE TIME INDEX IN BACKGROUND
        // ADD NEXT CHUNK OF BACKGROUND AUDIO
        // Play the background audio.
        
        explosion_triggered_ = true;
        // Check for explosion trigger and play bomb audio if triggered.
        if (explosion_triggered_) {
            
            float sampleRate = background_audio_.getSampleRate();
            
            double Q = 5;
            double Ca = 340;  //air speed
            double Cs = 6000;  //solid speed
            double h = 10;   //drone height
            double d = 30;   //distance from the blast

            //Time delay through air propagation
            double airDist = sqrt(d*d + h*h);
            double airTimeDelay = 0.34 * pow(airDist, (1.4)) * pow(Q, (-0.2)) / Ca;
            double avgAirSpeed = airDist / airTimeDelay;

            // Time delay first through ground then air 
            double seismicTimeDelay = 0.91 * pow(d, (1.03)) * pow(Q, (-0.02)) / Cs;  
            double seismicToAirTimeDelay = h / avgAirSpeed;
            double seismicAirTimeDelay = seismicTimeDelay + seismicToAirTimeDelay;
        
            // IF SEISMIC_BOOM ADD SEISMIC_BOOM AUDIO
            AudioFile<float> seismicAudio = bomb_audio_;
            
            // TODO: low-pass filtering the seismic audio
            
            // COMPUTE TIME INDEX IN BACKGROUND
            size_t seismicDelayIdx = sampleRate * seismicAirTimeDelay;
//            std::cout << "seismicAirTimeDelay = " << seismicAirTimeDelay << std::endl;
            if (seismicDelayIdx < background_audio_.samples[0].size() && 
                    (seismicAudio.samples[0].size() < background_audio_.samples[0].size())) {
                for (size_t i = 0; i < seismicAudio.samples[0].size(); ++i) 
                    background_audio_.samples[0][i+seismicDelayIdx] += seismicAudio.samples[0][i];
            } else {
                std::cout << "Seismic signal starting index is out of range." << std::endl;
            }
            
//          PublishAudioMessage(seismicAudio);
//    
//          // IF AIR_BOOM ADD AIR_BOOM AUDIO
            AudioFile<float> airBoomAudio = bomb_audio_;
            // COMPUTE TIME INDEX IN BACKGROUND
            size_t airDelayIdx = sampleRate * airTimeDelay;
            if (airDelayIdx < background_audio_.samples[0].size() && 
                    (airBoomAudio.samples[0].size() < background_audio_.samples[0].size())) {
                for (size_t i = 0; i < airBoomAudio.samples[0].size(); ++i) 
                    background_audio_.samples[0][i+airDelayIdx] += airBoomAudio.samples[0][i];
            } else {
                std::cout << "Air signal starting index is out of range." << std::endl;
            }
            
//            PublishAudioMessage(airBoomAudio);
         
            explosion_triggered_ = false; // Reset the trigger after playing the bomb audio.
        }
        
        PublishAudioMessage(background_audio_);
        
    }
    
    /////////////////////////////////////////////////

    void GazeboBlast3DMicrophonePlugin::LoadAudioFiles(const std::string& background_file, const std::string& bomb_file) {
        
        bool loadedOK = background_audio_.load(background_file);
        if (!loadedOK) {
            gzerr << "Failed to load background audio file: " << background_file << std::endl;
        }

        loadedOK = bomb_audio_.load(bomb_file);
        if (!loadedOK) {
            gzerr << "Failed to load bomb audio file: " << bomb_file << std::endl;
        }
    }

    /////////////////////////////////////////////////
    
    void GazeboBlast3DMicrophonePlugin::PublishAudioMessage(AudioFile<float> audio) {
        
        
        int bitDepth = audio.getBitDepth();
        float sampleRate = audio.getSampleRate();
        std::vector<float> sampleData = audio.samples[0]; // in the channel 0
        int numChannels = audio.getNumChannels();    
        size_t numBytes = sampleData.size() * sizeof(float);
        
#if GAZEBO_MAJOR_VERSION >= 9
        common::Time now = world_->SimTime();
#else
        common::Time now = world_->GetSimTime();
#endif
        
        audio_message.mutable_header()->mutable_stamp()->set_sec(
                    now.Double());
        audio_message.mutable_header()->mutable_stamp()->set_nsec(
                    now.Double() * 1e9);
        audio_message.mutable_header()->set_frame_id("drone");
        audio_message.set_samplerate(sampleRate);
        audio_message.add_sampledata(reinterpret_cast<const char*>(sampleData.data()), numBytes);
        audio_message.set_bitspersample(bitDepth);

        audio_pub_->Publish(audio_message);
        
        // For Debugging, by saving a newly generated audio file with gain:
//        AudioFile<float> a;
//        a.setNumChannels (numChannels);
//        a.setNumSamplesPerChannel (sampleData.size());
//        a.setBitDepth (bitDepth);
//        
//        for (int i = 0; i < a.getNumSamplesPerChannel(); i++)
//        {
//            for (int channel = 0; channel < a.getNumChannels(); channel++)
//            {
//                a.samples[channel][i] = audio.samples[channel][i];
//            }
//        }
//        
//        std::string filePath = blast3d_audio_datafolder_ + "audioFile.wav"; // change this to somewhere useful for you
//        a.save ("audioFile.wav", AudioFileFormat::Wave);
    }
}
/* vim: set et fenc=utf-8 ff=unix sts=0 sw=2 ts=2 : */