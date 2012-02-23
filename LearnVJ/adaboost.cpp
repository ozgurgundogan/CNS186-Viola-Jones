#include "adaboost.h"
#include "features.h"
#include "../IntegralImage/integral_image.h"

#include "cv.h"
#include "highgui.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>

using namespace std;
using namespace cv;

const char* const base_positive = "../Faces_Normalized/";
const char* const base_negative = "../Negative_Dataset/";

vector<AdaBoostFeature*> RunAdaBoost(int which_faces, int which_not_faces, int how_many, int total_set) {
    vector<AdaBoostFeature*> container;
    // Generate integral images.
    vector<Mat> pos_iis;
    vector<double> pos_weights;
    vector<Mat> neg_iis;
    vector<double> neg_weights;
    Mat img_placeholder;
    Mat gray_img_placeholder;
    for(int i=0; i < which_faces; ++i) {
        char buffer[7];
        sprintf(buffer, "%d", i);
        char buffer2[100];
        strcat(buffer2, base_positive); strcat(buffer2, buffer);
        img_placeholder = imread(buffer2, 1);
        cvtColor(img_placeholder, gray_img_placeholder, CV_RGB2GRAY);
        pos_iis.push_back(IntegralImage(gray_img_placeholder)); 
    }
    for(int i=0; i < pos_iis.size(); ++i) {
        pos_weights.push_back((double)(1)/(double)(2 * pos_iis.size()));       
    }
    for(int i=0; i < which_not_faces; ++i) {
        char buffer[7];
        sprintf(buffer, "%d", i);
        char buffer2[100];
        strcat(buffer2, base_negative); strcat(buffer2, buffer);
        img_placeholder = imread(buffer2, 1);
        cvtColor(img_placeholder, gray_img_placeholder, CV_RGB2GRAY);
        neg_iis.push_back(IntegralImage(gray_img_placeholder)); 
    }
    for(int i=0; i < neg_iis.size(); ++i) {
        neg_weights.push_back((double)(1)/(double)(2 * neg_iis.size()));
    }

    // Generate random features.
    set<Feature*>* random_features = GenerateRandomFeatures(total_set);

    // Run AdaBoost rounds.
    for(int i=0; i < how_many; ++i) {
       container.push_back(RunAdaBoostRound(pos_iis, neg_iis, &pos_weights, &neg_weights, random_features));
    }
     
    return container;
}

AdaBoostFeature* RunAdaBoostRound(const vector<Mat> pos_iis, const vector<Mat> neg_iis, vector<double>* pos_weights, 
                                  vector<double>* neg_weights, set<Feature*>* feature_set) {
    // Step 1. Normalize the weights
    double weight_sums = 0;
    vector<double>::iterator it;
    for(it = pos_weights->begin(); it != pos_weights->end(); ++it) {
        weight_sums += *it;
    }
    for(it = neg_weights->begin(); it != neg_weights->end(); ++it) {
        weight_sums += *it;
    }
    for(it = pos_weights->begin(); it != pos_weights->end(); ++it) {
        *it /= weight_sums;
    }
    for(it = neg_weights->begin(); it != neg_weights->end(); ++it) {
        *it /= weight_sums;
    }

    // Step 2. Find the feature with the best error
    int best_threshold, best_polarity, cur_threshold, cur_polarity;
    double best_error, cur_error;
    Feature* best_feature;
    best_error = numeric_limits<double>::infinity(); 
    set<Feature*>::const_iterator feature_it;
    vector<Mat>::const_iterator im_it;
    vector<int> positive_results;
    vector<int> negative_results;
    for(feature_it = feature_set->begin(); feature_it != feature_set->end(); ++feature_it) {
        positive_results.clear();
        negative_results.clear();
        for(im_it = pos_iis.begin(); im_it != pos_iis.end(); ++im_it) {
            positive_results.push_back(CalculateFeature(*feature_it, *im_it));
        }
        for(im_it = neg_iis.begin(); im_it != neg_iis.end(); ++im_it) {
            negative_results.push_back(CalculateFeature(*feature_it, *im_it));
        }

        FindThresholdAndPolarity(positive_results, negative_results, pos_weights, neg_weights, &cur_threshold,
                                 &cur_polarity, &cur_error);

        if(cur_error < best_error) {
            best_error = cur_error;
            best_threshold = cur_threshold;
            best_polarity = cur_polarity;
            best_feature = *feature_it;
        }
    }

    // Step 3. Update the weights and return the correct feature.
    positive_results.clear();
    negative_results.clear();
    for(im_it = pos_iis.begin(); im_it != pos_iis.end(); ++im_it) {
        positive_results.push_back(CalculateFeature(best_feature, *im_it));
    }
    for(im_it = neg_iis.begin(); im_it != neg_iis.end(); ++im_it) {
        negative_results.push_back(CalculateFeature(best_feature, *im_it));
    }
    double beta = (best_error)/(1 - best_error);
    int cur_im = 0;
    for(it = pos_weights->begin(); it != pos_weights->end(); ++it) {
        // Correctly identified as true, reduce weight. Else leave the same
        if(best_polarity * positive_results[cur_im] < best_polarity * best_threshold) {
            *it *= beta;
        }
        ++cur_im;
    }
    cur_im = 0;
    for(it = neg_weights->begin(); it != neg_weights->end(); ++it) {
        // Correctly identified as false, reduce weight. Else leave the same
        if(best_polarity * negative_results[cur_im] >= best_polarity * best_threshold) {
            *it *= beta;
        }
        ++cur_im;
    }
   
    AdaBoostFeature* result = new AdaBoostFeature();
    result->feature = best_feature;
    result->threshold = best_threshold;
    result->polarity = best_polarity;
    result->beta_t = beta;
}

// Yes this is inefficient. I am lazy. QED.
// Try every example value as the threshold. This is WLOG.
void FindThresholdAndPolarity(const vector<int> positive_examples, const vector<int> negative_examples, 
                              vector<double>* pos_weights, vector<double>* neg_weights, int* threshold, int* polarity, 
                              double* error) {
    vector<int>::const_iterator it;
    int cur_threshold, best_threshold;
    int best_polarity;
    double pos_pol_sum, neg_pol_sum;
    double best_error;
    for(it = positive_examples.begin(); it != positive_examples.end(); ++it) {
        pos_pol_sum = 0; neg_pol_sum = 0;
        cur_threshold = *it;
        for(int i=0; i < positive_examples.size(); ++i) {
            // Misclasified with polarity of 1
            if(positive_examples[i] >= cur_threshold) { 
                pos_pol_sum += pos_weights->at(i);
            }
            // Misclassified with polarity of -1
            if(positive_examples[i] * -1 >= cur_threshold * -1) {
                neg_pol_sum += pos_weights->at(i);
            }
        }
        for(int i=0; i < negative_examples.size(); ++i) {
            // Misclasified with polarity of 1
            if(negative_examples[i] < cur_threshold) { 
                pos_pol_sum += neg_weights->at(i);
            }
            // Misclassified with polarity of -1
            if(negative_examples[i] * -1 < cur_threshold * -1) {
                neg_pol_sum += neg_weights->at(i);
            }
        }
        if(pos_pol_sum < best_error) {
            best_threshold = cur_threshold;
            best_polarity = 1;
            best_error = pos_pol_sum;
        }
        if(neg_pol_sum < best_error) {
            best_threshold = cur_threshold;
            best_polarity = -1;
            best_error = neg_pol_sum;
        }
    } 
    for(it = negative_examples.begin(); it != negative_examples.end(); ++it) {
        pos_pol_sum = 0; neg_pol_sum = 0;
        cur_threshold = *it;
        for(int i=0; i < positive_examples.size(); ++i) {
            // Misclasified with polarity of 1
            if(positive_examples[i] >= cur_threshold) { 
                pos_pol_sum += pos_weights->at(i);
            }
            // Misclassified with polarity of -1
            if(positive_examples[i] * -1 >= cur_threshold * -1) {
                neg_pol_sum += pos_weights->at(i);
            }
        }
        for(int i=0; i < negative_examples.size(); ++i) {
            // Misclasified with polarity of 1
            if(negative_examples[i] < cur_threshold) { 
                pos_pol_sum += neg_weights->at(i);
            }
            // Misclassified with polarity of -1
            if(negative_examples[i] * -1 < cur_threshold * -1) {
                neg_pol_sum += neg_weights->at(i);
            }
        }
        if(pos_pol_sum < best_error) {
            best_threshold = cur_threshold;
            best_polarity = 1;
            best_error = pos_pol_sum;
        }
        if(neg_pol_sum < best_error) {
            best_threshold = cur_threshold;
            best_polarity = -1;
            best_error = neg_pol_sum;
        }
    }
    *threshold = best_threshold;
    *polarity = best_polarity;
    *error = best_error;
}

void SaveAdaBoost(vector<AdaBoostFeature*> to_save, char* filename) {
    ofstream save_file;
    save_file.open(filename);
    vector<AdaBoostFeature*>::const_iterator it;
    for(it = to_save.begin(); it != to_save.end(); ++it) {
        save_file << (*it)->feature->type << " " << (*it)->feature->x1 << " " << (*it)->feature->y1 << " " << (*it)->feature->x2 
            << " " << (*it)->feature->y2 << " " << (*it)->threshold << " " << (*it)->polarity << " " << (*it)->beta_t << "\n";
    }
}
