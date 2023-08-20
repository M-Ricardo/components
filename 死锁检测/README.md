CSDN：[实现死锁检测组件](https://blog.csdn.net/Ricardo2/article/details/131612005)

创建5个线程，1申请2、2申请3、3申请4、4申请5、5申请1  
出现死锁
![image](https://github.com/M-Ricardo/components/assets/49547846/52c5a54c-b50c-4a51-993a-606b82871ed4)


修改1申请2、2申请3、3申请4、4申请5  
检测完成，未出现死锁
![image](https://github.com/M-Ricardo/components/assets/49547846/b6ea7317-10b6-4749-963c-6cff7c2aead3)
