#pragma once
#include <limits>
#include "../dependencies/eigen-master/Eigen/Dense"
#include <numeric> //std::iota
#include <algorithm> //std::sort, std::stable_sort
#include <vector>
#include <fstream>
#include <iostream>
#include <thread>
#include <future>
#include "constants.h"

using namespace Eigen;

//implements relative method - do not use for comparing with zero
//use this most of the time, tolerance needs to be meaningful in your context
template<typename TReal>
static bool check_if_approximately_equal(TReal a, TReal b, TReal tolerance = std::numeric_limits<TReal>::epsilon())
{
    if(std::isinf(a) && std::isinf(b) && std::signbit(a)==std::signbit(b))
        return true;

    TReal diff = std::fabs(a - b);
    if (diff <= tolerance)
        return true;

    if (diff < std::fmax(std::fabs(a), std::fabs(b)) * tolerance)
        return true;

    return false;
}

//supply tolerance that is meaningful in your context
//for example, default tolerance may not work if you are comparing double with float
template<typename TReal>
static bool check_if_approximately_zero(TReal a, TReal tolerance = std::numeric_limits<TReal>::epsilon())
{
    if (std::fabs(a) <= tolerance)
        return true;
    return false;
}

VectorXd calculate_gaussian_errors(const VectorXd &y,const VectorXd &predicted)
{
    VectorXd errors{y-predicted};
    errors=errors.array()*errors.array();
    return errors;
}

VectorXd calculate_logit_errors(const VectorXd &y,const VectorXd &predicted)
{
    VectorXd errors{-y.array() * predicted.array().log()  -  (1.0-y.array()).array() * (1.0-predicted.array()).log()};
    return errors;
}

VectorXd calculate_poisson_errors(const VectorXd &y,const VectorXd &predicted)
{
    VectorXd errors{predicted.array() - y.array()*predicted.array().log()};
    return errors;
}

VectorXd calculate_gamma_errors(const VectorXd &y,const VectorXd &predicted)
{
    VectorXd errors{predicted.array().log() - y.array().log() + y.array()/predicted.array()-1};
    return errors;
}

VectorXd calculate_poissongamma_errors(const VectorXd &y,const VectorXd &predicted)
{
    VectorXd errors{y.array().pow(0.5).array() / (-0.25) + y.array()*predicted.array().pow(-0.5) / 0.5 + predicted.array().pow(0.5) / 0.5};
    return errors;
}

//Computes errors (for each observation) based on error metric for a vector
VectorXd calculate_errors(const VectorXd &y,const VectorXd &predicted,const VectorXd &sample_weight=VectorXd(0),const std::string &family="gaussian")
{   
    //Error per observation before adjustment for sample weights
    VectorXd errors;
    if(family=="gaussian")
        errors=calculate_gaussian_errors(y,predicted);
    else if(family=="logit")
        errors=calculate_logit_errors(y,predicted);
    else if(family=="poisson")
        errors=calculate_poisson_errors(y,predicted);
    else if(family=="gamma")
        errors=calculate_gamma_errors(y,predicted);
    else if(family=="poissongamma")
        errors=calculate_poissongamma_errors(y,predicted);
    //Adjusting for sample weights if specified
    if(sample_weight.size()>0)
        errors=errors.array()*sample_weight.array();
    
    return errors;
}

double calculate_gaussian_error_one_observation(double y,double predicted)
{
    double error{y-predicted};
    error=error*error;
    return error;
}

//Computes error for one observation based on error metric
double calculate_error_one_observation(double y,double predicted,double sample_weight=NAN_DOUBLE)
{   
    //Error per observation before adjustment for sample weights
    double error{calculate_gaussian_error_one_observation(y,predicted)};    
    
    //Adjusting for sample weights if specified
    if(!std::isnan(sample_weight))
        error=error*sample_weight;

    return error;
}

//Computes overall error based on errors from calculate_errors(), returning one value
double calculate_error(const VectorXd &errors,const VectorXd &sample_weight=VectorXd(0))
{   
    double error{std::numeric_limits<double>::infinity()};

    //Adjusting for sample weights if specified
    if(sample_weight.size()>0)
        error=errors.sum()/sample_weight.sum();
    else
        error=errors.mean();
    
    return error;
}

VectorXd transform_zero_to_negative(const VectorXd &linear_predictor)
{
    VectorXd transformed_linear_predictor{linear_predictor};
    for (size_t i = 0; i < static_cast<size_t>(transformed_linear_predictor.rows()); ++i)
    {
        bool row_is_not_negative{std::isgreaterequal(transformed_linear_predictor[i],0.0)};
        if(row_is_not_negative)
            transformed_linear_predictor[i]=SMALL_NEGATIVE_VALUE;
    }
    return transformed_linear_predictor;
}

VectorXd transform_linear_predictor_to_predictions(const VectorXd &linear_predictor, const std::string &family="gaussian")
{
    if(family=="gaussian")
        return linear_predictor;
    else if(family=="logit")
    {
        VectorXd exp_of_linear_predictor{linear_predictor.array().exp()};
        return exp_of_linear_predictor.array() / (1.0 + exp_of_linear_predictor.array());
    }
    else if(family=="poisson")
        return linear_predictor.array().exp();
    else if(family=="gamma")
    {
        VectorXd transformed_linear_predictor{transform_zero_to_negative(linear_predictor)};
        return -1/transformed_linear_predictor.array();
    }
    else if(family=="poissongamma")
    {
        VectorXd transformed_linear_predictor{transform_zero_to_negative(linear_predictor)};
        return transformed_linear_predictor.array().pow(-2).array() * 4.0;
    }
    return VectorXd(0);
}

double transform_linear_predictor_to_prediction(double linear_predictor, const std::string &family="gaussian")
{
    if(family=="gaussian")
        return linear_predictor;
    else if(family=="logit")
    {
        double exp_of_linear_predictor{std::exp(linear_predictor)};
        return exp_of_linear_predictor / (1.0 + exp_of_linear_predictor);
    }
    else if(family=="poisson")
        return std::exp(linear_predictor);
    else if(family=="gamma")
    {
        double negative_linear_predictor{std::min(linear_predictor,SMALL_NEGATIVE_VALUE)};
        return -1/negative_linear_predictor;
    }
    else if(family=="poissongamma")
    {
        double negative_linear_predictor{std::min(linear_predictor,SMALL_NEGATIVE_VALUE)};
        return 4.0 * std::pow(negative_linear_predictor,-2);
    }
    return NAN_DOUBLE;
}

//sorts index based on v
VectorXi sort_indexes_ascending(const VectorXd &v)
{
    // initialize original index locations
    VectorXi idx(v.size());
    std::iota(idx.begin(),idx.end(),0);

    // sort indexes based on comparing values in v
    std::sort(idx.begin(), idx.end(),[&v](size_t i1, size_t i2) {return v(i1) < v(i2);});

    return idx;
}

//Loads a csv file into an Eigen matrix
template<typename M>
M load_csv (const std::string &path) {
    std::ifstream indata;
    indata.open(path);
    std::string line;
    std::vector<double> values;
    uint rows = 0;
    while (std::getline(indata, line)) {
        std::stringstream lineStream(line);
        std::string cell;
        while (std::getline(lineStream, cell, ',')) {
            values.push_back(std::stod(cell));
        }
        ++rows;
    }
    return Map<const Matrix<typename M::Scalar, M::RowsAtCompileTime, M::ColsAtCompileTime, RowMajor>>(values.data(), rows, values.size()/rows);
}

//Saves an Eigen matrix as a csv file
void save_data(std::string fileName, MatrixXd matrix)
{
    //https://eigen.tuxfamily.org/dox/structEigen_1_1IOFormat.html
    const static IOFormat CSVFormat(FullPrecision, DontAlignCols, ", ", "\n");
 
    std::ofstream file(fileName);
    if (file.is_open())
    {
        file << matrix.format(CSVFormat);
        file.close();
    }
}

//For multicore distribution of elements
struct DistributedIndices
{
    std::vector<size_t> index_lowest;
    std::vector<size_t> index_highest; 
};

//Distribution of elements to multiple cores
template <typename T> //type must implement a size() method
DistributedIndices distribute_to_indices(T &collection,size_t n_jobs)
{
    size_t collection_size=static_cast<size_t>(collection.size());

    //Initializing output
    DistributedIndices output;
    output.index_lowest.reserve(collection_size);
    output.index_highest.reserve(collection_size);

    //Determining how many items to evaluate per core
    size_t available_cores{static_cast<size_t>(std::thread::hardware_concurrency())};
    if(n_jobs>1)
        available_cores=std::min(n_jobs,available_cores);
    size_t units_per_core{std::max(collection_size/available_cores,static_cast<size_t>(1))};

    //For each set of items going into one core
    for (size_t i = 0; i < collection_size; i=i+units_per_core) 
    {                
        output.index_lowest.push_back(i); 
    }
    for (size_t i = 0; i < output.index_lowest.size()-1; ++i)
    {
        output.index_highest.push_back(output.index_lowest[i+1]-1);
    }
    output.index_highest.push_back(collection_size-1);
    //Removing last bunch and adjusting the second last if necessary
    if(output.index_lowest.size()>available_cores) 
    {
        output.index_lowest.pop_back();
        output.index_highest.pop_back();
        output.index_highest[output.index_highest.size()-1]=collection_size-1;
    }

    return output;
}

template <typename T> //type must implement a size() method
size_t calculate_max_index_in_vector(T &vector)
{
    return vector.size()-static_cast<size_t>(1);
}

template <typename T> //type must be an Eigen Matrix or Vector
bool check_if_matrix_has_nan_or_infinite_elements(const T &x)
{
    bool matrix_has_nan_or_infinite_elements{!x.allFinite()};
    if(matrix_has_nan_or_infinite_elements)
        return true;
    else
        return false;
}

template <typename T> //type must be an Eigen Matrix or Vector
void throw_error_if_matrix_has_nan_or_infinite_elements(const T &x, const std::string &matrix_name)
{
    bool matrix_is_empty{x.size()==0};
    if(matrix_is_empty) return;

    bool matrix_has_nan_or_infinite_elements{check_if_matrix_has_nan_or_infinite_elements(x)};
    if(matrix_has_nan_or_infinite_elements)
    {
        throw std::runtime_error(matrix_name + " has nan or infinite elements.");
    }
}