#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <algorithm>
#include <limits>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#define WHITE cv::Scalar(255, 255, 255)
#define BLACK cv::Scalar(0, 0, 0)

namespace po = boost::program_options;
namespace fs = boost::filesystem;

std::map<std::string, std::vector<cv::Rect>> labelMap;
std::string imageName;
bool displayDefectInfo = true;

// save the current markerSize for editing
int markerSize = 5;
// save the current overlay factor
int overlay = 35;
// save if quitting is required
bool quit = false;
// save if image name should be displayed
bool display_filename = false;

cv::Point mousePosition;
cv::Rect zoomRect;

/**
 * Project the current mouse position back onto the original image given
 * a zoomed in rectangle.
 *
 */
cv::Point
global_pos(cv::Mat* imageGT) {
    float zoomWidthFactor = zoomRect.width / static_cast<float>(imageGT->cols);
    float zoomHeightFactor = zoomRect.height / static_cast<float>(imageGT->rows);
    return cv::Point(zoomRect.x + (mousePosition.x * zoomWidthFactor), zoomRect.y + (mousePosition.y * zoomHeightFactor));
}

/**
 * Compute the zooming rectangle provided a zooming factor and the image zoomed into.
 * Zooming in corresponds to factors in (0.0, 1.0) and zooming out corresponds to
 * factors larger than 1.
 * Zooming is done in regard to the current zooming rectangle. Zooming in will be
 * done in a way that the cursor will stay on the same location of the image.
 * In contrast, zooming out can jump a bit in order to keep the rectanlge valid.
 */
void
zoom(double factor, cv::Mat* image) {
    // store the current position of the cursor without zooming
    cv::Point zoomPosition = global_pos(image);

    // these raios have to stay the same so that the mouse cursor stays on the same position
    double width_ratio = mousePosition.x / static_cast<double>(image->cols);
    double height_ratio = mousePosition.y / static_cast<double>(image->rows);

    // scale width and height according the provided factor
    // make sure that the zooming rectangle is not larger than the image zoomed into
    zoomRect.width = std::min(image->cols, static_cast<int>(zoomRect.width * factor));
    zoomRect.height = std::min(image->rows, static_cast<int>(zoomRect.height * factor));

    // change x position in a way that makes the mouse cursor stay on the same position
    zoomRect.x = std::max(0, static_cast<int>(zoomPosition.x - (width_ratio * zoomRect.width)));
    // make sure that the rectangle does not focus parts outside the image
    // this can only happen on zooming out and can lead to 'jumping'
    zoomRect.x = std::min(zoomRect.x, image->cols - zoomRect.width);

    // change y position in a way that makes the mouse cursor stay on the same position
    zoomRect.y = std::max(0, static_cast<int>(zoomPosition.y - (height_ratio * zoomRect.height)));
    // make sure that the rectangle does not focus parts outside the image
    // this can only happen on zooming out and can lead to 'jumping'
    zoomRect.y = std::min(zoomRect.y, image->rows - zoomRect.height);
}

void mark(cv::Mat *imageGT, const bool asGT) {
    const cv::Scalar color = asGT ? WHITE : BLACK;
    const cv::Point globalCursorPos = global_pos(imageGT);

    cv::Point topLeft(globalCursorPos.x - markerSize, globalCursorPos.y - markerSize);
    cv::Point bottomRight(topLeft.x + markerSize, topLeft.y + markerSize);
    cv::rectangle(*imageGT, topLeft, bottomRight, color, CV_FILLED);
}

/**
 * Callback handling mouse events. It saves the last mouse position, marks regions as
 * salient on left click, un-marks regions on right click and zoom in or out on mouse-wheel
 * events.
 */
void onMouse(int event, int x, int y, int flags, void* userdata) {
    // save current mouse position
    // skip saving on mouse wheel events because it somehow results in negative values
    if (event != cv::EVENT_MOUSEWHEEL && event != cv::EVENT_MOUSEHWHEEL) {
        mousePosition = cv::Point(x, y);
    }

    cv::Mat *imageGT = (cv::Mat*) userdata;
    if (event == cv::EVENT_LBUTTONDOWN || (event == cv::EVENT_MOUSEMOVE && flags & cv::EVENT_FLAG_LBUTTON)) {
        mark(imageGT, true);
    } else if (event == cv::EVENT_RBUTTONDOWN || (event == cv::EVENT_MOUSEMOVE && flags & cv::EVENT_FLAG_RBUTTON)) {
        mark(imageGT, false);
    } else if (event == cv::EVENT_MOUSEMOVE) {
        if (flags & cv::EVENT_FLAG_LBUTTON) {
            mark(imageGT, true);
        } else if (flags & cv::EVENT_FLAG_RBUTTON) {
            mark(imageGT, false);
        }
    } else if (event == cv::EVENT_MOUSEHWHEEL) {
        bool inwards = cv::getMouseWheelDelta(flags) < 0;

        if (flags & cv::EVENT_FLAG_CTRLKEY) {
            // if ctrl key is pressed zoom in/out
            if (inwards) {
                zoom(0.95, imageGT);
            } else {
                zoom(1.0 / 0.95, imageGT);
            }
        } else if (flags & cv::EVENT_FLAG_SHIFTKEY) {
            // modify overlay if shift key is pressed (tried alt key but it did not work)
            if (inwards) {
                overlay = std::min(overlay + 5, 100);
            } else {
                overlay = std::max(overlay - 5, 0);
            }
            cv::setTrackbarPos("Blending", "AnnotationTool", overlay);
        } else {
            // modify marker size without modifier keys pressed
            if (inwards) {
                markerSize = std::min(markerSize + 1, 50);
            } else {
                markerSize = std::max(markerSize - 1, 1);
            }
            cv::setTrackbarPos("Size", "AnnotationTool", markerSize);
        }
    }
}

/**
 * Empty callback because property is directly bound to trackbar.
 */
void onTrackbarSizeChange(int event, void* userdata) {
}

/**
 * Empty callback because property is directly bound to trackbar.
 */
void onTrackbarBlendingChange(int event, void* userdata) {
}

/**
 * Create a vector of files in a given directory.
 */
std::vector<fs::path> get_files_from_dir(std::string dir) {
    // create vector
    std::vector<fs::path> files;

    // create end iterator
    fs::directory_iterator end_itr;
    // iterate over entries in directory
    for (fs::directory_iterator itr(dir); itr != end_itr; itr++) {
        // skip directories
        if (fs::is_directory(itr->path())) {
            continue;
        }

        // add file to vector
        files.push_back(itr->path());
    }

    // return vector
    return files;
}

/**
 * Create an image to display to the user. This image contains a zoomed in blend between the image
 * and the GT on the left side, the current GT on the top right and control information
 * on the bottom right side.
 */
cv::Mat
create_image_to_show(cv::Mat* image, cv::Mat* imageGT, std::string image_file = "") {
    // create image to show with enough space to display blend, GT and info
    cv::Mat image_to_show(image->rows, image->cols + (0.5 * image->cols), image->type());

    // create blended image
    cv::Mat blend;
    // add weighted GT to image to create blend
    cv::addWeighted(*image, 1.0, *imageGT, overlay / 100.0, 0.0, blend);

    if (displayDefectInfo && labelMap.find(imageName) != labelMap.end()) {
        for (const cv::Rect r : labelMap[imageName]) {
            cv::rectangle(blend, r, cv::Scalar(255, 0, 0), 2);
        }
    }

    // create zoomed as part of image to show
    cv::Mat zoomed(image_to_show, cv::Rect(0, 0, image->cols, image->rows));
    // zoom in by projecting rectangle of blend onto zoomed
    cv::resize(blend(zoomRect), zoomed, blend.size());

    // draw marker
    double zoomFactor = imageGT->cols / static_cast<double>(zoomRect.width);
    cv::Point topLeft(mousePosition.x - (markerSize * zoomFactor) - 1, mousePosition.y - (markerSize * zoomFactor) - 1);
    cv::Point bottomRight(topLeft.x + (markerSize * zoomFactor) + 1, topLeft.y + (markerSize * zoomFactor) + 1);
    cv::rectangle(image_to_show, topLeft, bottomRight, BLACK, 1);

    return zoomed;
}

/**
 * Display a provided image and its GT for a user to interactively annotate it.
 */
int
annotate_image(cv::Mat* image, cv::Mat* imageGT, std::string image_file = "") {
    // create resizable window
    cv::namedWindow("AnnotationTool", cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO | cv::WINDOW_GUI_EXPANDED);
    // set initial window size
    cv::resizeWindow("AnnotationTool", 1600, 900);

    // add callback to handle mouse events
    cv::setMouseCallback("AnnotationTool", onMouse, imageGT);
    // add trackbars and callbacks
    cv::createTrackbar("Size", "AnnotationTool", &markerSize, 50, onTrackbarSizeChange);
    cv::createTrackbar("Blending", "AnnotationTool", &overlay, 100, onTrackbarBlendingChange);

    // cv::Rect zoomRect;
    zoomRect = cv::Rect(0, 0, imageGT->cols, imageGT->rows);

    // display image
    cv::imshow("AnnotationTool", create_image_to_show(image, imageGT));

    // iterate as long as user is not finished
    while (true) {
        // handle events at 60 fps
        int key = cv::waitKey(1000 / 60);

        // handle key events
        switch (key) {
            case 'n':
            case '\n':
                // n, enter -> save and go to next image
                return 1;
            case 'p':
            case 8:  // BACKSPACE
                // p, backspace -> save and go to previous image
                return -1;
            case 'q':
            case 27:  // ESC
                // esc or q -> quit without doing anything
                quit = true;
                return 0;
            case '+':
                // + increase markerSize
                markerSize += 5;
                cv::setTrackbarPos("Size", "AnnotationTool", markerSize);
                break;
            case '-':
                // - decrease markerSize
                markerSize -= 5;
                cv::setTrackbarPos("Size", "AnnotationTool", markerSize);
                break;
            case 'i':
                display_filename = !display_filename;
                break;
            case 'f':
                // do not zoom if cursor is outside the image
                if (mousePosition.x > imageGT->cols || mousePosition.y > imageGT->rows) {
                    break;
                }

                // zoom in
                zoom(0.8, imageGT);
                break;
            case 'g':
                // do not zoom if cursor is outside the image
                if (mousePosition.x > imageGT->cols || mousePosition.y > imageGT->rows) {
                    break;
                }

                // zoom out
                zoom(1.0 / 0.8, imageGT);
                break;
            case 'G':
                // zoom out completely
                zoomRect.x = 0;
                zoomRect.y = 0;
                zoomRect.width = imageGT->cols;
                zoomRect.height = imageGT->rows;
                break;
            // Note: w,a,s,d are used for moving instead of the arrow keys because
            // the arrow keys seem to change the trackbars in openCV per default
            case 'a':
                // move zooming rectangle left relative to its width
                zoomRect.x = std::max(0, zoomRect.x - static_cast<int>(0.2 * zoomRect.width));
                break;
            case 'w':
                // move zooming rectangle up left relative to its height
                zoomRect.y = std::max(0, zoomRect.y - static_cast<int>(0.2 * zoomRect.height));
                break;
            case 'd':
                // move zooming rectangle right relative to its width
                zoomRect.x = std::min(imageGT->cols - zoomRect.width, zoomRect.x + static_cast<int>(0.2 * zoomRect.width));
                break;
            case 's':
                // move zooming rectangle down relative to its height
                zoomRect.y = std::min(imageGT->rows - zoomRect.height, zoomRect.y + static_cast<int>(0.2 * zoomRect.height));
                break;
            case 'z':
                displayDefectInfo = !displayDefectInfo;
                break;
            default:
                // uncomment to find out keys by number
                // std::cout << "Key: " << key << std::endl;
                break;
        }
        cv::Mat image_to_show = create_image_to_show(image, imageGT, display_filename ? image_file : "");
        // re-render image
        cv::imshow("AnnotationTool", image_to_show);
    }

    return 0;
}

/**
 * Annotate a list of images from a provided directory and save them at the provided
 * output directory. An index can be specified to skip this many images from the directory.
 */
void
annotate(std::string image_dir, std::string output_dir, int start_index, std::string skipTo) {
    std::vector<fs::path> files = get_files_from_dir(image_dir);

    // read in files that were already annotated
    std::string annotatedFileName = output_dir + "/.annotated.txt";
    std::vector<std::string> alreadyAnnotatedFiles;
    std::ifstream annotationFile(annotatedFileName);
    for (std::string line; std::getline(annotationFile, line); ) {
       alreadyAnnotatedFiles.push_back(line);
    }

    // save index and if it should be skipped to a certain file
    int i = start_index;
    bool skipped = skipTo == "";
    bool alreadyAnnotated;
    while (i < files.size()) {
        // retrieve current file from array
        fs::path image_file = files[i];
        // load input image
        cv::Mat image = cv::imread(image_file.string());
        std::cout << i << "/" << files.size() << " - Loaded Image: " << image_file << std::endl;
        if (image.empty()) {
            std::cout << "Could not load image " << image_file << "!" << std::endl;
            continue;
        }
        imageName = image_file.string().substr(image_file.string().size() - 10, 6);
        alreadyAnnotated = std::find(alreadyAnnotatedFiles.begin(), alreadyAnnotatedFiles.end(), imageName) != alreadyAnnotatedFiles.end();
        if (!skipped && skipTo != imageName) {
            i++;
            continue;
        } else {
            if (skipped && alreadyAnnotated) {
                i++;
                continue;
            }
            skipped = true;
        }

        // create output file matching to input image
        std::string output_file = output_dir + "/" + image_file.filename().string();
        // create GT
        cv::Mat imageGT;
        if (fs::exists(output_file)) {
            // if already available load matching GT
            imageGT = cv::imread(output_file);
            std::cout << "Loaded GT: " << output_file << std::endl;
        } else {
            // create black GT
            imageGT = cv::Mat(image.size(), CV_8UC3, BLACK);
        }

        // display GUI to annotate, returns when jumping to next/previous image is required
        i += annotate_image(&image, &imageGT, image_file.filename().string());

        // quit if value was set
        if (quit) {
            return;
        }

        // save annotated GT
        cv::Mat output_image;
        cv::cvtColor(imageGT, output_image, cv::COLOR_RGB2GRAY);
        cv::imwrite(output_file, output_image);

        // save that image was annotated
        if (!alreadyAnnotated) {
            std::ofstream annotationFileO(annotatedFileName, std::ios_base::app | std::ios_base::out);
            annotationFileO << imageName << "\n";
            annotationFileO.close();
        }
    }
}

/**
 * Main method which starts the annotation GUI.
 */
int
main(int argc, char** argv) {
    std::ifstream labelFile("manlabel.txt");
    std::string filename, defectType;
    int xMin, yMin, xMax, yMax;
    // the images as expected here are extracted part of the original images,
    // thus, the rectanlges have to be moved to fit the extracted part
    // how much they need to be moved is saved in this map
    std::map<std::string, cv::Point> anchorPointMap;
    while (labelFile >> filename >> yMin >> xMin >> yMax >> xMax >> defectType) {
        if (anchorPointMap.find(filename) == anchorPointMap.end()) {
            anchorPointMap[filename] = cv::Point(std::numeric_limits<int>().max(), std::numeric_limits<int>().max());
        }
        anchorPointMap[filename].x = std::min(anchorPointMap[filename].x, xMin);
        anchorPointMap[filename].y = std::min(anchorPointMap[filename].y, yMin);

        if (defectType == "sound") {
            continue;
        }

        labelMap[filename].push_back(cv::Rect(xMin, yMin, xMax - xMin, yMax - yMin));
    }
    for (auto& entry : labelMap) {
        for (cv::Rect& rect : entry.second) {
            rect -= anchorPointMap[entry.first];
        }
    }

    // create variables with default values
    int start_index;
    std::string output_dir;
    std::string skipTo;

    // add program options
    po::options_description desc("GUI to annotate images from within a specified directory. Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
        ("image_dir", po::value<std::string>(), "set the directory of images to be annotated")
        ("output_dir,o", po::value<std::string>(&output_dir)->default_value("GT"), "set the directory where the annotated images will be stored")
        ("start_index", po::value<int>(&start_index)->default_value(0), "set the start index")
        ("skip_to", po::value<std::string>(&skipTo)->default_value(""), "set the name of the image file to which it should be skipped")
    ;

    // mark image dir as a positional option
    po::positional_options_description p;
    p.add("image_dir", 1);

    // pare options
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm);

    // handle help request
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    // make sure image directory is always specified
    if (!vm.count("image_dir")) {
        std::cout << "Error! An image directory has to be specified!\n" << desc << std::endl;
        return 1;
    }

    // retrieve image directory
    std::string image_dir = vm["image_dir"].as<std::string>();
    // make sure that image directory is really a directory
    if (!fs::exists(image_dir) || !fs::is_directory(image_dir)) {
        std::cout << "Error! Image directory[" << image_dir << "] is not available!" << std::endl;
        return 1;
    }

    // make sure that output directory exists and is a directory
    // if it does not yet exist create it
    if (fs::exists(output_dir)) {
        if (!fs::is_directory(output_dir)) {
            std::cout << "Error! Output directory[" << output_dir << "] is not a directory!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Create output directory[" << output_dir << "]" << std::endl;
        fs::create_directory(output_dir);
    }

    // start annotation
    annotate(image_dir, output_dir, start_index, skipTo);
    return 0;
}
