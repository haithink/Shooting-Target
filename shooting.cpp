#include <iostream>
#include <vector>
#include <dirent.h>
#include <algorithm>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace std;
using namespace cv;

// keyboard code for enter
const int ENTER = 10;
// scale the picture to 1000 columns
const int COLS = 1000;
// low and high threshold 
const int DELTA = 60;
// how large is the target area
// for air pistol, it is ring8 to ring10
const int TARGET_SIZE = 3;
// max score
const double MAX_SCORE = 10.0;
// max number of shoot on each target
// use this to calculate the overlap
const int MAX_SHOOT = 10;

bool loadAndScale(const string& filename, Mat& pic){
    Mat orig = imread(filename);
    if(orig.empty()){
        cout<<"can not find: "<<filename<<endl;
        return false;
    }
    Size dsize = Size(COLS, float(COLS) / float(orig.cols) * orig.rows);
    resize(orig, pic, dsize);
    return true;
}

void onMouseCatchColor(int mouseEvent, int x, int y, int flags, void* param){
    switch(mouseEvent){
        case CV_EVENT_LBUTTONDOWN:{
            Point* p = (Point*)param;
            cout<<"catch color at: ("<<y<<", "<<x<<")    "<<endl;
            *p = Point(x, y);
            break;
        }
        default:{}
    }
}

void RGB2HSVEqualize(const Mat& picRGB, Mat& picHSV){    
    cvtColor(picRGB, picHSV, COLOR_BGR2HSV);
    // equalize
    /*
    vector<Mat> hsvSplit;
    split(picHSV, hsvSplit);
    equalizeHist(hsvSplit[2],hsvSplit[2]);
    merge(hsvSplit,picHSV);
    */
}

void catchColor(const Mat& pic, Vec3b& background, Vec3b& target){
    namedWindow("catch background color");
    imshow("catch background color", pic);
    Point p_back, p_tar;
    setMouseCallback("catch background color", onMouseCatchColor, &p_back);
    while(true){
        int key = waitKey(0);
        if(key == ENTER)
            break;
    }
    destroyWindow("catch background color");

    namedWindow("catch target color");
    imshow("catch target color", pic);
    setMouseCallback("catch target color", onMouseCatchColor, &p_tar);
    while(true){
        int key = waitKey(0);
        if(key == ENTER)
            break;
    }
    destroyWindow("catch target color");
    
    Mat picHSV;    
    RGB2HSVEqualize(pic, picHSV);

    background = picHSV.at<Vec3b>(p_back);
    target = picHSV.at<Vec3b>(p_tar);

    cout<<"background color: "<<background<<endl;
    cout<<"target color: "<<target<<endl;
}

void getMaxContour(const vector<vector<Point> >& contours, Point& c, double& r){
    double max_size = 0.0;
    vector<vector<Point> >::const_iterator which;
    for(vector<vector<Point> >::const_iterator iter = contours.begin(); iter != contours.end(); ++iter){
        double area = contourArea(*iter);
        if(area > max_size){
            max_size = area;
            which = iter;
        }
    }
    cout<<"max size contour at "<<which - contours.begin()<<" with size "<<max_size<<endl;
    float radius;
    Point2f center;
    minEnclosingCircle(Mat(*which), center, radius);
    c = Point(center);
    r = radius;
}

void getAllContours(const vector<vector<Point> >& contours, vector<Point>& centers, vector<double>& radius){
    // use this to solve the overlapping of shooting marks
    vector<pair<double, int> > area;
    for(int i=0; i<contours.size(); i++)
        area.push_back(make_pair(contourArea(contours[i]), i));
    sort(area.begin(), area.end());

    for(int i=0; i<contours.size(); i++){
        float r;
        Point2f c;
        minEnclosingCircle(Mat(contours[i]), c, r);
        centers.push_back(Point(c));
        radius.push_back(r);
    }

    // add additional shooting marks here
    for(int i=0; i<(MAX_SHOOT - contours.size()); i++){
        float r;
        Point2f c;
        minEnclosingCircle(Mat(contours[ area[i].second ]), c, r);
        centers.push_back(Point(c));
        radius.push_back(r);
    }
}

void rangeThreshold(const Mat& picHSV, const Vec3b& color, Mat& picThreshold){
    Vec3b low = color - Vec3b(DELTA, DELTA, DELTA);
    Vec3b high = color + Vec3b(DELTA, DELTA, DELTA);

    inRange(picHSV, low, high, picThreshold);

    // reduce noise
    Mat element = getStructuringElement(MORPH_RECT, Size(5, 5));
    morphologyEx(picThreshold, picThreshold, MORPH_OPEN, element);
    morphologyEx(picThreshold, picThreshold, MORPH_CLOSE, element);
    
    // debug, show the threshold result
    namedWindow("threshold");
    imshow("threshold", picThreshold);
    waitKey(0);
    destroyWindow("threshold");
}

void contourTarget(const Mat& picHSV, const Vec3b& color, Point& c, double& r){
    Mat picThreshold;
    rangeThreshold(picHSV, color, picThreshold);

    vector<vector<Point> > contours;
    findContours(picThreshold, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE);
    cout<<"find "<<contours.size()<<" contours"<<endl;

    getMaxContour(contours, c, r);

    // debug, show the contour circle
    Mat result(picThreshold.size(),CV_8U,Scalar(0));
    circle(result, c, static_cast<int>(r), Scalar(255), 2);
    namedWindow("contours");
    imshow("contours", result);
    waitKey(0);
    destroyWindow("contours");
}

void contourBackground(const Mat& picHSV, const Vec3b& color, vector<Point>& cs, vector<double>& rs){
    Mat picThreshold;
    rangeThreshold(picHSV, color, picThreshold);

    vector<vector<Point> > contours;
    findContours(picThreshold, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE);
    cout<<"find "<<contours.size()<<" contours"<<endl;

    getAllContours(contours, cs, rs);
}

void lsDir(const string& dirName, vector<string>& files){
    DIR* dir = opendir(dirName.data());
    struct dirent *ptr;
    while((ptr = readdir(dir)) != NULL){
        if(ptr->d_name[0] != '.')
            files.push_back(dirName + "/" + string(ptr->d_name));
    }
    closedir(dir);
}

double getScore(const vector<Point>& shoot, const Point& center, const double ppr){
    // ppr means pixel per ring
    double avg = 0.0;
    for(vector<Point>::const_iterator iter = shoot.begin(); iter != shoot.end(); ++iter){
        double dist = norm(*iter - center);
        double ring = dist / ppr;
        double score = MAX_SCORE - ring;
        //cout<<score<<endl;
        avg += score;
    }
    return avg / double(shoot.size());
}

void shootingScore(const vector<string>& filenames){
    // catch background and target color in the first picture
    Mat pic;
    loadAndScale(filenames[1], pic);
    Vec3b background, target;
    cout<<"select color on: "<<filenames[1]<<endl;
    catchColor(pic, background, target);

    for(vector<string>::const_iterator iter = filenames.begin(); iter != filenames.end(); ++iter){
        // then, for every picture, calculate the score
        loadAndScale(*iter, pic);
        Mat picHSV;
        RGB2HSVEqualize(pic, picHSV);

        //imwrite("hsv" + *iter, picHSV);

        Point target_center;
        double target_radius;
        contourTarget(picHSV, target, target_center, target_radius);

        vector<Point> background_centers;
        vector<double> background_radius;
        contourBackground(picHSV, background, background_centers, background_radius);

        double ppr = target_radius / TARGET_SIZE;
        double avg = getScore(background_centers, target_center, ppr);
        cout<<"average score is: "<<avg<<endl;
    }
}

int main(int argc, char const *argv[])
{
    vector<string> strs;
    lsDir("origin", strs);
    shootingScore(strs);
    return 0;
}
