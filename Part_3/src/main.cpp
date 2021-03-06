/*Chapter 3:模型的加载与初始化
 * author: TchaikovskyBear
 * 2020-08-05
 * */

#include <iostream>
#include <unistd.h>
#include <thread>
#include "video_service.hpp"
#include "utils_service.hpp"
#include "event_service.hpp"
#include "algorithm_service.hpp"
#include "common.hpp"
#include "array_queue.hpp"
#include "SSDModel.hpp"
#include "img_save.hpp"

using namespace std;



int main() {
    int ret;
    int duration_num = 12;//每次只取duration_num帧数据
    char app_name[100] = "SmokeDetect";/*app的名字,必须与封包时的app名相同,否则无法显示检测框*/
    ArrayQueue array_queue(12, sizeof(SDC_YUV_DATA_S));
    /* 申请视频服务,构造函数会执行 注册服务 + 申请yuv_channel id + 导入队列*/
    VideoService video_service(&array_queue);
    /*申请工具服务,这个服务主要用于SDC的辅助操作,比如内存的申请和释放等*/
    UtilsService utils_service;
    /*申请事件服务*/
    EventService event_service(app_name);
    /* 申请算法模块服务*/
    AlgorithmService algorithm_service(&video_service, &utils_service);

/*---核心流程---
 * 1 加载模型
 * 2 读取图像
 * 3 模型推导
 * 4 获得结果
 * --------------*/

/*--- 1 加载SSD模型---------------------------------------------------------------------------------------------------*/
//NNIE_ssd3d_12input_with_norm_inst ssd_mask_inst0909
    char model_file[100] = "./res/model/NNIE_ssd3d_12input_with_norm_inst.wk";
    ret = algorithm_service.SDC_load_model(model_file, 1);
    if (ret < 0) {
        DEBUG_LOG("ERR:SDC_load_model failed.");
    } else
        DEBUG_LOG("load model sucess.");
    /*声明一个ssd模型对象*/
    SSDModel ssd(&algorithm_service,
                 &utils_service,
                 &event_service,
                 algorithm_service.get_model(),
                 duration_num,9,300, 300, 0.6);

    /*模型初始化*/
    ret = ssd.ssd_param_init(1, 0);
    if (ret < 0) {
        DEBUG_LOG("ERR:ssd_param_init failed.");
        exit(0);
    } else
        DEBUG_LOG("ssd_param_init sucess.");

/*--- 2 读取图像------------------------------------------------------------------------------------------------------*/
    //设定视频通道的参数
    video_service.set_yuv_channel_param(304, 300, 12);
    //订阅数据
    video_service.subscribe_video(12);
    //使用线程将订阅的数据不停的保存到数组队列中
    std::thread video_thread(&VideoService::read_camera_data_run, &video_service);
    //取出数据并释放

    UINT32 box_num;
    int idx;
    SDC_YUV_DATA_S yuv_data[duration_num];
    SDC_YUV_FRAME_S rgb_data[duration_num];//储存转换后的rgb数据
    VW_YUV_FRAME_S input_rgb_data[duration_num];
    /*将前端摄像头获取的数据与后端的模型输入blob进行地址连接,后端的模型将会从该地址拷贝数据作为模型输入*/
    ssd.data_connect(input_rgb_data);
    /*使用线程循环执行前向推导*/
    int loop_condition = 1;
    while (loop_condition) {
        ret = video_service.get_data_from_queue(yuv_data, duration_num,NULL);
        if (ret == PAS) {
            /*补充新数据*/
            for (idx = 0; idx < duration_num; idx++) {
//                DEBUG_LOG("添加新数据,idx:%i",idx);
                /*将YUV420SP数据转为RGB*/
                ret = algorithm_service.SDC_TransYUV2RGB(&yuv_data[idx].frame, &rgb_data[idx]);
                if (ret < 0) {
                    DEBUG_LOG("ERR:SDC_TransYUV2RGB failed.");
                    exit(-1);
                } else {
                    /*将数据中RGB各个通道的内存地址找出,储存在更适合模型前向推导的SDC_YUV_FRAME_S数据结构中*/
                    VideoService::SDC_Struct2RGB(&rgb_data[idx], &input_rgb_data[idx]);
                }
            }
/*--- 3 模型推导------------------------------------------------------------------------------------------------------*/
            /*每获得一帧数据,调用模型进行一次推导*/
            box_num = ssd.infer();

/*--- 4 获得结果------------------------------------------------------------------------------------------------------*/
            ssd.show(box_num,app_name,yuv_data[duration_num-1].pts);
            /*释放转换后的rgb数据内存*/
            for(idx = 0;idx<duration_num;idx++){
                algorithm_service.SDC_TransYUV2RGBRelease(&rgb_data[idx]);
                /*释放YUV420SP数据的内存空间*/
//                video_service.release_yuv(&yuv_data[idx]);
            }
            /*释放YUV420SP数据的内存空间*/
            video_service.release_yuv(&yuv_data[0]);

        }
        //我们一秒取25帧,也就是40000微妙一帧,为了更好的观测到队列为空时的返回值,所以延迟30000微妙
        usleep(10000);

    }

    while (1) {
        sleep(5);
    }
}