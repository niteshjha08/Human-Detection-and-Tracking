/******************************************************************************
 * MIT License

Copyright (c) 2022 Nitesh Jha, Tanuj Thakkar

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
* *******************************************************************************
*/

/**
 * @copyright Copyright (c) 2022 Nitesh Jha, Tanuj Thakkar
 * @file detector.cpp
 * @author Nitesh Jha (Driver), Tanuj Thakkar (Navigator)
 * @brief Detection class definition for AcmePerception
 * @version 0.1
 * @date 2022-10-19
 *
 *
 */
#include "detector.hpp"

#include <unistd.h>

#include <fstream>

// Constructor
Detector::Detector() {}

// Destructor
Detector::~Detector() {}

// Set size of input required by detector model
void Detector::setInputSize(const cv::Size size) {
  input_width_ = size.width;
  input_height_ = size.height;
}

// set score threshold
void Detector::setScoreThreshold(const double score_thresh) {
  score_thresh_ = score_thresh;
}

// set confidence threshold
void Detector::setConfidenceThreshold(const double conf_thresh) {
  confidence_thresh_ = conf_thresh;
}

// set NMS threshold
void Detector::setNMSThreshold(const double nms_thresh) {
  nms_thresh_ = nms_thresh;
}

// set all classes to detect by detector model
void Detector::setClassesToDetect(const std::vector<std::string> classes) {
  classes_to_detect_ = classes;
}

// set model path of detector and initialize detector model
void Detector::setModelPath(const std::string model_path) {
  model_path_ = model_path;
  network_ = cv::dnn::readNet(model_path_);
}

// set classes_list_ from class_list_path containing all classes of detection
void Detector::setClassList(const std::string class_list_path) {
  std::ifstream ifs(class_list_path);
  std::string line;
  while (getline(ifs, line)) {
    classes_list_.push_back(line);
  }
}

// forward pass of detector model
std::vector<cv::Mat> Detector::runInference(cv::Mat &blob) {
  std::vector<cv::Mat> outputs;
  network_.setInput(blob);
  network_.forward(outputs, network_.getUnconnectedOutLayersNames());

  return outputs;
}

// run inference, filter detections, NMS and drawing outputs
cv::Mat Detector::detect(cv::Mat &input_blob, cv::Mat &input_image) {
  // clear all values of previous detection
  resetDetector();

  // Forward Pass
  std::vector<cv::Mat> detections = runInference(input_blob);

  // Filter low confidence detections and low class scores, and keep only
  // classes_to_detect
  filterDetections(input_image, detections);

  // Non-maximum suppression
  cv::Mat result = NMS(input_image);

  return result;
}

void Detector::filterDetections(cv::Mat &input_image,
                                std::vector<cv::Mat> &outputs) {
  // Resizing factor used in preprocessing for drawing bbox on original image
  float ratio_x = input_image.cols / input_width_;
  float ratio_y = input_image.rows / input_height_;
  float *data = (float *)outputs[0].data;
  // total rows in a detection: bbox coordinates(4), confidence, class scores
  // (80)
  const int dimensions = 85;

  // Total number of detections
  const int rows = 25200;

  // Check all detections for confidence scores, class scores, and filter to
  // retain classes to detect
  for (int i = 0; i < rows; ++i) {
    float confidence = data[4];
    // Eliminate low confidence detections
    if (confidence >= confidence_thresh_) {
      float *classes_scores = data + 5;
      // 1x85 matrix to hold class scores of all classes.
      cv::Mat scores(1, classes_list_.size(), CV_32FC1, classes_scores);
      // get index of best class  score.
      cv::Point class_id;
      double max_class_score;
      cv::minMaxLoc(scores, 0, &max_class_score, 0, &class_id);
      // check if best class score is above the threshold and if class is one we
      // want to detect
      if (max_class_score > score_thresh_ &&
          std::find(classes_to_detect_.begin(), classes_to_detect_.end(),
                    classes_list_[class_id.x]) != classes_to_detect_.end()) {
        // Push bboxes, confidence values, and class names to member vectors
        confidences_.push_back(confidence);
        class_ids_.push_back(class_id.x);
        // center coordinates
        float cx = data[0];
        float cy = data[1];
        // bbox size
        float w = data[2];
        float h = data[3];
        // bbox coordinates
        int left = int((cx - 0.5 * w) * ratio_x);
        int top = int((cy - 0.5 * h) * ratio_y);
        int width = int(w * ratio_x);
        int height = int(h * ratio_y);
        bboxes_.push_back(cv::Rect(left, top, width, height));
      }
    }
    // Go to the next row.
    data += dimensions;
  }
}

// perform Non-maximum-suppression and draw bbox, confidence, labels for
// NMS-filtered bboxes
cv::Mat Detector::NMS(cv::Mat &input_image) {
  std::vector<int> indices;
  cv::dnn::NMSBoxes(bboxes_, confidences_, score_thresh_, nms_thresh_, indices);
  for (int i = 0; i < indices.size(); i++) {
    int idx = indices[i];
    cv::Rect box = bboxes_[idx];
    int left = box.x;
    int top = box.y;
    int width = box.width;
    int height = box.height;
    // draw bounding box and confidence value in text
    cv::rectangle(input_image, cv::Point(left, top),
                  cv::Point(left + width, top + height),
                  cv::Scalar(255, 178, 50), 3);
    // Get the label for the class name and its confidence.
    std::string label = cv::format("%.2f", confidences_[idx]);

    label = classes_list_[class_ids_[idx]] + ":" + label;
    // Draw class labels
    drawLabel(input_image, label, left, top);
  }
  return input_image;
}

// draw output image
void Detector::drawLabel(cv::Mat &input_image, std::string label, int left,
                         int top) {
  int baseLine;
  cv::Size label_size =
      cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.7, 1, &baseLine);
  top = std::max(top, label_size.height);
  // top left corner
  cv::Point tlc = cv::Point(left, top);
  // bottom right corner
  cv::Point brc =
      cv::Point(left + label_size.width, top + label_size.height + baseLine);
  // draw white rectangle.
  cv::rectangle(input_image, tlc, brc, cv::Scalar(0, 0, 0), cv::FILLED);
  // put the label on the black rectangle
  cv::putText(input_image, label, cv::Point(left, top + label_size.height),
              cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 1);
}

// clear all values from previous detection
void Detector::resetDetector() {
  class_ids_.clear();
  confidences_.clear();
  bboxes_.clear();
}
