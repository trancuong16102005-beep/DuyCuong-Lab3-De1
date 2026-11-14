# DuyCuong-Lab3-De1
 Mạch Giám sát và cảnh báo người chạy (runner)
 Họ và tên : Trần Duy Cương 
https://wokwi.com/projects/480648506685368321
1. Giới thiệu

Dự án xây dựng một hệ thống giám sát môi trường dựa trên vi điều khiển ESP32, có khả năng đo nhiệt độ, áp suất và độ cao theo thời gian thực. Hệ thống tự đưa ra cảnh báo tại chỗ bằng đèn LED và còi, đồng thời gửi dữ liệu lên trang web để người dùng theo dõi và điều khiển từ xa. Điểm nổi bật của dự án là chương trình được tổ chức trên hệ điều hành thời gian thực FreeRTOS. Các công việc được chia thành nhiều task chạy song song trên hai nhân của ESP32, giúp việc đo đạc và cảnh báo luôn ổn định, không bị ảnh hưởng bởi tình trạng kết nối mạng. Hệ thống được mô phỏng trên nền tảng Wokwi và giao tiếp với trang web thông qua giao thức MQTT.

2. Chức năng

Hệ thống đọc dữ liệu từ cảm biến BMP180 theo chu kỳ đều đặn mỗi giây, gồm nhiệt độ và áp suất khí quyển, rồi tính ra độ cao tương ứng. Dựa trên số liệu này, hệ thống tự động cảnh báo. Đèn LED sáng khi nhiệt độ vượt ra ngoài khoảng cho phép. Còi kêu khi độ cao thay đổi đột ngột, ví dụ khi thiết bị bị nâng lên hoặc rơi xuống.

Toàn bộ số liệu đo cùng trạng thái của đèn và còi được gửi lên trang web dưới dạng chuỗi JSON để hiển thị trực quan. Ngoài chế độ tự động, người dùng có thể điều khiển thủ công từ web: bật, tắt LED hoặc còi, hoặc đưa hệ thống trở về chế độ tự động. Chế độ thủ công tự hết hiệu lực sau 15 giây, hoặc bị hủy ngay khi xảy ra biến cố độ cao mới, để đảm bảo cảnh báo quan trọng không bị bỏ qua. Bên cạnh đó, hệ thống in thông tin hoạt động ra cổng Serial để phục vụ theo dõi và gỡ lỗi.

3. Linh kiện và giao thức sử dụng

Thành phần trung tâm là bo mạch ESP32 DevKit-C V4. Bo mạch này có vi xử lý hai nhân, tích hợp sẵn WiFi, đủ mạnh để vừa xử lý cảm biến vừa duy trì kết nối mạng.

Cảm biến BMP180 dùng để đo nhiệt độ và áp suất. Cảm biến giao tiếp với ESP32 qua chuẩn I2C, với chân SDA nối GPIO21 và chân SCL nối GPIO22, được cấp nguồn 3,3 V. Đèn LED đỏ nối với GPIO14 thông qua điện trở hạn dòng 220 Ω, dùng để báo nhiệt độ bất thường. Còi buzzer nối với GPIO19, được phát âm tần số 2000 Hz để báo biến cố độ cao.

Về giao thức, hệ thống sử dụng ba giao thức:

I2C để trao đổi dữ liệu giữa ESP32 và cảm biến.
WiFi để ESP32 kết nối vào mạng. Trong môi trường mô phỏng, ESP32 tự quét và chọn mạng Wokwi-GUEST hoặc CirkitWifi.
MQTT để truyền dữ liệu giữa ESP32 và trang web thông qua broker công cộng broker.emqx.io, cổng 1883.

Ngoài ra, cổng Serial tốc độ 115200 baud được dùng để xuất thông tin theo dõi.

4. Nguyên lý hoạt động

Khi được cấp nguồn, ESP32 lần lượt thực hiện các bước khởi tạo sau:

Cấu hình cổng Serial và các chân điều khiển LED, còi.
Tạo các hàng đợi và cơ chế đồng bộ của FreeRTOS.
Kiểm tra cảm biến BMP180. Nếu không tìm thấy cảm biến, hệ thống báo lỗi và dừng lại.
Nếu cảm biến hoạt động bình thường, tạo ba task và phân bổ chúng lên hai nhân xử lý.

Từ đây, ba task chạy song song và lặp liên tục.

Task đọc dữ liệu cứ mỗi giây đọc nhiệt độ và áp suất từ cảm biến, rồi tính độ cao theo công thức khí áp. Kết quả được gửi theo hai hướng: gói nhiệt độ và độ cao đến task cảnh báo, gói đầy đủ đến task truyền.

Task cảnh báo nhận dữ liệu, so sánh với các ngưỡng đã cài đặt và kiểm tra lệnh từ web. Sau đó nó quyết định bật hoặc tắt LED, còi, rồi cập nhật trạng thái mới.

Task truyền dữ liệu duy trì kết nối WiFi và MQTT, đóng gói số liệu cùng trạng thái hệ thống thành chuỗi JSON và gửi lên web. Đồng thời, task này nhận lệnh điều khiển từ web và chuyển cho task cảnh báo xử lý.

Nhờ cách tổ chức này, dữ liệu đi từ cảm biến đến ESP32, qua WiFi và broker MQTT rồi tới trang web. Lệnh điều khiển đi theo chiều ngược lại từ web xuống ESP32. Các task chỉ trao đổi với nhau qua hàng đợi, nên mỗi phần hoạt động độc lập và không làm ảnh hưởng lẫn nhau.

5. Ngưỡng cảnh báo

Các ngưỡng cảnh báo được khai báo thành hằng số ở đầu chương trình. Muốn thay đổi, chỉ cần sửa giá trị tại một vị trí duy nhất.

Với nhiệt độ, hệ thống dùng hai ngưỡng: T_MIN bằng 15 °C và T_MAX bằng 37,5 °C. Khi nhiệt độ nằm trong khoảng này, LED tắt. Khi nhiệt độ thấp hơn 15 °C hoặc cao hơn 37,5 °C, LED sáng để báo nhiệt độ bất thường.

Với độ cao, hệ thống không so sánh với một mốc cố định mà so với trung bình của ba lần đo trước đó. Nếu lần đo mới lệch quá ALT_JUMP bằng 3 mét, độ cao được coi là thay đổi đột ngột. Khi đó còi kêu và được giữ trong ALT_HOLD_MS bằng 3000 ms. Việc dùng giá trị trung bình giúp lọc nhiễu, tránh còi kêu nhầm do sai số của cảm biến. Thời gian hiệu lực của lệnh thủ công từ web được quy định bởi MANUAL_TIMEOUT_MS bằng 15000 ms.

Khi quyết định đầu ra, hệ thống xét theo thứ tự ưu tiên:

Chế độ thủ công, nếu đang có lệnh từ web.
Biến cố độ cao: bật còi.
Nhiệt độ ngoài ngưỡng: bật LED.
Không có cảnh báo: tắt cả LED và còi.

Ngoài ra, hệ thống tuân theo một luật chung: LED và còi không bao giờ bật cùng lúc, và còi luôn được ưu tiên vì báo hiệu tình huống khẩn cấp hơn.

6. Cơ chế FreeRTOS

Chương trình được tổ chức thành ba task, mỗi task đảm nhận một nhiệm vụ riêng và được gán cố định vào một nhân của ESP32.

Hai task cần đáp ứng đúng thời gian được đặt trên Core 1. Task cảnh báo có mức ưu tiên 3, là mức cao nhất. Task đọc dữ liệu có mức ưu tiên 2. Vì FreeRTOS lập lịch theo kiểu chiếm quyền, ngay khi task đọc gửi dữ liệu mới, task cảnh báo được đánh thức và chạy trước. Nhờ vậy cảnh báo luôn có độ trễ nhỏ nhất.

Task truyền dữ liệu có mức ưu tiên 1 và được đặt riêng trên Core 0, cùng nhân với phần xử lý WiFi của hệ thống. Khi mạng chậm hoặc mất kết nối, việc đo và cảnh báo trên Core 1 vẫn diễn ra bình thường.

Các task không dùng biến chung mà giao tiếp qua bốn hàng đợi:

qAlert chuyển dữ liệu từ task đọc sang task cảnh báo.
qData chuyển dữ liệu từ task đọc sang task truyền. Khi hàng đợi đầy, gói cũ nhất bị bỏ để luôn giữ số liệu mới nhất.
qLenh chuyển lệnh từ web sang task cảnh báo.
qTrangThai là hàng đợi một phần tử, hoạt động như một hộp thư. Task cảnh báo ghi đè trạng thái mới vào đây, còn task truyền chỉ xem mà không lấy ra.

Bên cạnh hàng đợi, hệ thống dùng hai cơ chế đồng bộ. Semaphore nhị phân semWiFi báo cho task truyền biết khi WiFi đã kết nối, giúp task này ngủ chờ thay vì liên tục kiểm tra và tốn CPU. Mutex mSerial bảo vệ cổng Serial, tránh việc nhiều task cùng in làm các dòng thông tin chen lẫn vào nhau. Task đọc dữ liệu dùng hàm vTaskDelayUntil để đảm bảo chu kỳ đo đúng một giây, không bị trôi theo thời gian. Hàm loop() mặc định của Arduino tự xóa để nhường toàn bộ CPU cho các task.

7. Hệ thống MQTT

MQTT là giao thức truyền tin nhẹ theo mô hình xuất bản và đăng ký (publish/subscribe), rất phù hợp với các thiết bị IoT. Trong mô hình này, ESP32 và trang web không kết nối trực tiếp với nhau. Cả hai cùng kết nối tới một máy chủ trung gian gọi là broker. Dự án sử dụng broker công cộng broker.emqx.io tại cổng 1883. Khi kết nối, ESP32 dùng một mã định danh ngẫu nhiên để tránh trùng với thiết bị khác.

Việc trao đổi dữ liệu diễn ra qua hai chủ đề (topic):

Chiều dữ liệu, topic lad3/runner/data: ESP32 xuất bản chuỗi JSON gồm nhiệt độ, áp suất, độ cao, trạng thái LED, trạng thái còi, chế độ hoạt động (tự động hoặc thủ công) và lý do cảnh báo. Trang web đăng ký topic này để nhận và hiển thị số liệu theo thời gian thực.
Chiều điều khiển, topic lad3/runner/cmd: trang web xuất bản các lệnh led_on, led_off, buzzer_on, buzzer_off và auto. ESP32 đăng ký topic này để nhận lệnh, sau đó chuyển lệnh cho task cảnh báo thực hiện.

Nếu kết nối MQTT bị gián đoạn, task truyền tự động kết nối lại sau mỗi hai giây. Trong thời gian đó, dữ liệu mới nhất vẫn được giữ trong hàng đợi để gửi đi ngay khi kết nối được khôi phục.

Nếu bạn cần đưa nội dung này vào báo cáo, mình có thể làm thành file Word kèm các ảnh sơ đồ đã vẽ ở trên.
