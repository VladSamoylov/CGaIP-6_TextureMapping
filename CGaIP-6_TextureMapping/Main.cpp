#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

const GLint WIDTH = 800, HEIGHT = 800; // Константи початкової ширини та висоти вікна
GLuint shader, uniformModel, uniformProjection, uniformView; // Глобальні змінні для об'єктів у пам'яті відеокарти shader - ID скомпільованої шейдерної програми, а uniformModel - ID для передачі матриці 4х4 у вершинний шейдер...
const float toRadians = 3.14159265f / 180.0f; // Коефіцієнт для переведення кутів із градусів у радіани
float curAngle = 0.0f; // Поточний кут повороту фігури
// Структура, що об'єднує дані одного об'єкта
struct Figure {
	GLuint VAO; // Ідентифікатор  масиву налаштувань буферів
	GLuint VBO; // Ідентифікатор буфера з чистими координатами у відеопам'яті
	GLuint IBO; // Ідентифікатор буфера індексів 
	int verteciesAmount; // Загальна кількість вершин у фігурі
};

// Створюємо текст вершиного шейдера, який використовується для перетворення вершин у відповідні координати на екрані
static const char* vShader = R"(
	#version 330 // Вказуємо відеокарті, що код написаний на мові GLSL версії 3.3
	// in (це вхідні дані, які приходять у шейдер ззовні)
	// vec3 (координати у форматі вектора 3-х чисел типу float для кожної точки)
	// layout (location = 0) номер порту, який зв'язує C++ масив із цим шейдером
	layout(location = 0) in vec3 pos; // Слот №0: координати вершини у форматі вектора 3-х чисел float (X, Y, Z)
	layout(location = 1) in vec2 aTexCoord; // Слот №1: текстурні UV-координати для цієї вершини (2 числа float: U, V)
	out vec2 TexCoord; // Вихідна змінна: прокидає UV-координати далі у фрагментний шейдер для інтерполяції	

	uniform mat4 model; // Створюємо Uniform-змінну для передачі матриці моделі 4х4
	uniform mat4 projection; // Створюємо Uniform-змінну для передачі матриці проєкції 4х4
	uniform mat4 view; // Створюємо Uniform-змінну для передачі матриці виду 4х4

	void main()	{ // Головна функція шейдера, яка викликається окремо для кожної вершини
			gl_Position = projection * view * model * vec4(pos, 1.0); // Обчислюємо фінальну позицію точки на екрані монітора шляхом послідовного перемноження матриць
			TexCoord = aTexCoord; // Передаємо текстурні координати без змін, растеризатор автоматично проінтерполює їх для кожного пікселя
	}
)";

// Створюємо текст фрагметного шейдера, який буде керувати кольором пікселей
static const char* fShader = R"(
	#version 330
	in vec2 TexCoord; // Вхідна змінна: приймає вже автоматично проінтерполовану UV-координату для поточного пікселя
	out vec4 FragColour; // out (вихідні дані, які шейдер відає назовні для подальшого відображення пікселю на екран)

	uniform sampler2D texture1;	

		void main()	{ // Головна функція шейдера, яка викликається окремо для кожного пікселя
			FragColour = texture(texture1, TexCoord); // Витягуємо чистий колір пікселя (текселя) з картинки за вказаними координатами
		}
)";

unsigned int loadTexture(const char* path) {
	unsigned int textureID;
	glGenTextures(1, &textureID); // Генеруємо унікальний ідентифікатор для текстури у відеопам'яті
	int width, height, nrChannels;
	stbi_set_flip_vertically_on_load(true); // Перевертаємо картинку по вертикалі при завантаженні, щоб узгодити системи координат JPEG (зверху-вниз) та OpenGL (знизу-вгору)
	unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0); // Читаємо файл картинки в масив байтів через бібліотеку stb_image
	if (data) {
		GLenum format = GL_RGB; // За замовчуванням формат трикомпонентний (RGB для .jpg)
		if (nrChannels == 1) format = GL_RED; // Якщо картинка чорно-біла
		else if (nrChannels == 3) format = GL_RGB; // Якщо картинка має канали R, G, B
		else if (nrChannels == 4) format = GL_RGBA; // Якщо картинка має прозорість (PNG)

		glBindTexture(GL_TEXTURE_2D, textureID); // Активуємо нашу текстуру для налаштування

		// Налаштування правил обгортки: повторення малюнка, якщо UV-координати вийдуть за межі [0.0, 1.0]
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // По горизонталі (вісь U)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); // По вертикалі (вісь V)

		// Налаштування фільтрації (сгладжування пікселів)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // Трилінійна фільтрація з міп-мапами для об'єктів вдалені (прибирає шум)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // Лінійна фільтрація для згладжування картинки при наближенні камери впритул


		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data); // Закачуємо чисті байти розпакованого зображення безпосередньо у відеокарту (VRAM)
		glGenerateMipmap(GL_TEXTURE_2D); // Автоматично генеруємо зменшені копії текстури (міп-мапи) для оптимізації рендерингу

		stbi_image_free(data);
	}
	else std::cout << "Error: non loading the texture on the path" << std::endl;

	return textureID; // Повертаємо готовий ID налаштованої текстури
}

// Функція створення фігури
Figure CreateFigure(GLfloat* vertices, int size, unsigned short* indices, int sizeIndices) {
	Figure newFig;
	newFig.verteciesAmount = sizeIndices / sizeof(unsigned short); // Рахуємо кількість вершин
	glGenVertexArrays(1, &newFig.VAO); // Створюємо VAO, яка буде запам'ятовувати всі налаштування буферів 
	glBindVertexArray(newFig.VAO); // Активуємо VAO

	glGenBuffers(1, &newFig.IBO); // Створюємо та ініціціалізуємо буфер індексів для оптимізації роботи з вершинами та підвищення ефективності малювання складних геометричних об'єктів
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, newFig.IBO); // Активуємо індексний буфер
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeIndices, indices, GL_STATIC_DRAW); // Копіюємо масив індексів з ОЗП до VRAM

	glGenBuffers(1, &newFig.VBO); // Створюємо VBO буфер, куди фізично завантажимо координати фігури
	glBindBuffer(GL_ARRAY_BUFFER, newFig.VBO); // Активуємо VBO
	glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW); // Копіюємо масив точок з ОЗП у VRAM

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0); // Створюємо декодер-пояснення як читати байти (слот №0, 3 числа, типу флоат, без нормалізації, крок, зміщення)
	glEnableVertexAttribArray(0); // Вмикаємо декодер для слоту 0

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1); // Вмикаємо декодер для слоту 1

	glBindBuffer(GL_ARRAY_BUFFER, 0); // Відв'язуємо буфери, щоб випадково не стерти дані в іншому місці програми
	glBindVertexArray(0);
	return newFig;
}

// Допоміжна функція для компіляції одного шейдера
void AddShader(GLuint theProgram, const char* shaderCode, GLenum shaderType)
{
	GLuint theShader = glCreateShader(shaderType); // Створюємо порожній об'єкт шейдера потрібного типу (вершинний або фрагментний)
	const GLchar* theCode[1];
	theCode[0] = shaderCode; // Передаємо текст програми нашого шейдера
	GLint codeLength[1];
	codeLength[0] = strlen(shaderCode); // Рахуємо довжину тексту
	glShaderSource(theShader, 1, theCode, codeLength); // Передаємо текст шейдера у створений порожній об'єкт шейдера
	glCompileShader(theShader); // Компілюємо шейдер
	// Перевіряємо на помилки в коді GLSL шейдера
	GLint result = 0;
	GLchar eLog[1024] = { 0 };
	glGetShaderiv(theShader, GL_COMPILE_STATUS, &result);
	if (!result)
	{
		glGetShaderInfoLog(theShader, 1024, NULL, eLog);
		fprintf(stderr, "Error compiling the %d shader:	'%s'\n", shaderType, eLog); // Якщо компіляція провалилася до виводимо інформацію
		return;
	}
	glAttachShader(theProgram, theShader); // Інакше прикріплюємо цей скомпільований шейдер до нашої програми
}

void CompileShaders() {
	shader = glCreateProgram(); // Створюємо програму-інструкцію для відеокарти
	if (!shader)
	{
		printf("Failed to create shader\n");
		return;
	}
	AddShader(shader, vShader, GL_VERTEX_SHADER); // Додаємо та компілюємо вершинний шейдер
	AddShader(shader, fShader, GL_FRAGMENT_SHADER); // Додаємо та компілюємо фрагментний шейдер
	GLint result = 0;
	GLchar eLog[1024] = { 0 };
	glLinkProgram(shader); // З'єднує шейдер в одну інструкцію
	// Перевіряємо чи успішно вони з'єдналися між собою
	glGetProgramiv(shader, GL_LINK_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shader, sizeof(eLog), NULL, eLog);
		printf("Error linking program: '%s'\n", eLog);
		return;
	}
	// Перевіряємо, чи зможе взагалі цей шейдер запуститися на поточній відеокарті
	glValidateProgram(shader);
	glGetProgramiv(shader, GL_VALIDATE_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shader, sizeof(eLog), NULL, eLog);
		printf("Error validating program: '%s'\n", eLog);
		return;
	}
	uniformModel = glGetUniformLocation(shader, "model"); // находимо адресу змінної "model" всередині шейдеру і записуємо її в глобальний ідентифікатор uniformModel
	uniformProjection = glGetUniformLocation(shader, "projection"); // находимо адресу змінної "projection" всередині шейдеру і записуємо її в глобальний ідентифікатор uniformProjection
	uniformView = glGetUniformLocation(shader, "view"); // находимо адресу змінної "view" всередині шейдеру і записуємо її в глобальний ідентифікатор uniformView
}

int main() {
	if (!glfwInit()) // Спроба ініціалізації бібліотеки GLFW, яка потрібна для створення віконих інтерфейсів та обробки подій в графічних програмах
	{
		printf("GLFW failed to start :(");
		glfwTerminate(); // У разі не ініціалізації бібліотеки GLFW виконується очищення ресурсів і завершення виконання програми
		return 1;
	}
	// Налаштовуємо OpenGL перед створенням вікна
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // Встановлюємо головну версію OpenGL 
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); // Встановлюємо додаткову версію OpenGL 
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // Використовуємо Core-профіль, профіль сучасного OpenGL без застарілих функцій
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Вмикаємо зворотну сумісність для коректної роботи з macOS та новими драйверами
	GLFWwindow* mainWindow = glfwCreateWindow(WIDTH, HEIGHT, "by Vladislav Samoilov KNT-223", NULL, NULL); // Створюємо головне вікно програми із заданими розмірами та заколовком
	if (!mainWindow) // Перевіряємо ініціалізацію вікна
	{
		printf("GLFW window creation failed :(");
		glfwTerminate(); // Закриваємо GLFW, якщо вікно не вдалося створити та завершаємо виконання програми
		return 1;
	}
	int bufferWidth, bufferHeight; // Змінні для збереження реального розміру області рендерингу (в пікселях)
	glfwGetFramebufferSize(mainWindow, &bufferWidth, &bufferHeight); // Отримуємо фактичний розмір буфера кадру вікна
	glfwMakeContextCurrent(mainWindow); // Робимо контекст OpenGL створеного вікна поточним для цього потоку програми
	glewExperimental = GL_TRUE; // Дозволяємо GLEW завантажувати експериментальні та нові розширення OpenGL
	if (glewInit() != GLEW_OK) // Перевіряємо ініціалізацію бібліотеки GLEW, який динамічно завантажує функції OpenGL для відеокарти
	{
		printf("GLEW failed to start >_<");
		glfwDestroyWindow(mainWindow); // Знищуємо створене вікно
		glfwTerminate(); // Завершуємо роботу GLFW
		return 1;
	}
	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, bufferWidth, bufferHeight); // Налаштовуємо область перегляду OpenGL

	// Координати вершин куба
	GLfloat squareV[] = {
		// Передня грань
		-1.0f, 1.0f, 1.0f, 0.0f, 1.0f, // Точка 0: лівий верхній кут
		-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, // Точка 1: лівий нижній кут
		1.0f, -1.0f, 1.0f, 1.0f, 0.0f, // Точка 2: правий нижній кут
		1.0f, 1.0f, 1.0f, 1.0f, 1.0f, // Точка 3: правий верхній кут

		// Права грань
		1.0f, 1.0f, 1.0f, 0.0f, 1.0f, // Точка 4: лівий верхній кут
		1.0f, -1.0f, 1.0f, 0.0f, 0.0f, // Точка 5: лівий нижній кут
		1.0f, -1.0f, -1.0f, 1.0f, 0.0f, // Точка 6: правий нижній кут
		1.0f, 1.0f, -1.0f, 1.0f, 1.0f, // Точка 7: правий верхній кут

		// Ліва грань
		-1.0f, 1.0f, -1.0f, 0.0f, 1.0f, // Точка 8: лівий верхній кут
		-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, // Точка 9: лівий нижній кут
		-1.0f, -1.0f, 1.0f, 1.0f, 0.0f, // Точка 10: правий нижній кут
		-1.0f, 1.0f, 1.0f, 1.0f, 1.0f, // Точка 11: правий верхній кут

		// Верхня грань
		-1.0f, 1.0f, -1.0f, 0.0f, 1.0f, // Точка 12: лівий верхній кут
		-1.0f, 1.0f, 1.0f, 0.0f, 0.0f, // Точка 13: лівий нижній кут
		1.0f, 1.0f, 1.0f, 1.0f, 0.0f, // Точка 14: правий нижній кут
		1.0f, 1.0f, -1.0f, 1.0f, 1.0f, // Точка 15: правий верхній кут

		// Нижня грань
		-1.0f, -1.0f, -1.0f, 0.0f, 1.0f, // Точка 16: лівий верхній кут
		-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, // Точка 17: лівий нижній кут
		1.0f, -1.0f, 1.0f, 1.0f, 0.0f, // Точка 18: правий нижній кут
		1.0f, -1.0f, -1.0f, 1.0f, 1.0f, // Точка 19: правий верхній кут

		// Задня грань
		-1.0f, 1.0f, -1.0f, 0.0f, 1.0f, // Точка 20: лівий верхній кут
		-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, // Точка 21: лівий нижній кут
		1.0f, -1.0f, -1.0f, 1.0f, 0.0f, // Точка 22: правий нижній кут
		1.0f, 1.0f, -1.0f, 1.0f, 1.0f // Точка 23: правий верхній кут
	};

	// Масив індексів для збірки куба (6 граней по 2 трикутника, разом 36 індексів для 12 унікальних трикутників)
	unsigned short indices[] = {
		0, 1, 2, // Передня грань (трикутник №1)
		0, 2, 3, // Передня грань (трикутник №2)
		4, 5, 6, // Права грань (трикутник №1)
		4, 6, 7, // Права грань (трикутник №2)
		8, 9, 10, // Ліва грань (трикутник №1)
		8, 10, 11, // Ліва грань (трикутник №2)
		12, 13, 14, // Верхня грань (трикутник №1)
		12, 14, 15, // Верхня грань (трикутник №2)
		16, 17, 18, // Нижня грань (трикутник №1)
		16, 18, 19, // Нижня грань (трикутник №2)
		20, 21, 22, // Задня грань (трикутник №1) 
		20, 22, 23  // Задня грань (трикутник №2) 
	};

	Figure squre = CreateFigure(squareV, sizeof(squareV), indices, sizeof(indices)); // Створюємо геометрію куба
	CompileShaders(); // Компілюємо шейдери
	// Створюємо перспективну матрицю проєкції: кут 90 градусів, пропорції екрана, межі видимості від 0.1 до 100 метрів
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), (GLfloat)bufferWidth / (GLfloat)bufferHeight, 0.1f, 100.0f);
	unsigned int itTexture = loadTexture("./textures/rusty_metal_04_diff_1k.jpg");

	while (!glfwWindowShouldClose(mainWindow)) // Головний цикл роботи програми, працює, поки користувач не закриє вікно
	{
		glfwPollEvents(); // Перевіряємо та обробляємо події
		
		glClearColor(0.0f, 0.3f, 1.0f, 1.0f); // Встановлюємо колір для очищення екрану RGBA
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Очищаємо буфер кольору кадру за допомогою вказаного вище кольору
		glUseProgram(shader); // Вмикаємо шейдерну програму
		glm::mat4 model(1.0f); // Ініціалізація одиничної матриці моделі
		// Створюємо статичну матрицю виду (положення камери, точка фокусування та вектор верху)
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)); 

		model = glm::rotate(model, -45.0f * toRadians, glm::vec3(0.0f, 1.0f, 0.0f)); // Додаємо трансформацію фігури: нахил на -45 градусів навколо осі Y

		glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(model)); // Передаємо готову фінальну матрицю 4х4 у вершинний шейдер без перевороту (GL_FALSE)
		glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projection)); // Передаємо перспективну матрицю проєкції у вершинний шейдер
		glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(view)); // Передаємо матрицю виду у вершинний шейдер
		glBindVertexArray(squre.VAO); // Вмикаємо налаштування фігури
		glActiveTexture(GL_TEXTURE0); // Активуємо нульовий текстурний юніт відеокарти
		glBindTexture(GL_TEXTURE_2D, itTexture); // Прив'язуємо нашу завантажену текстуру до активного юніту
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0); // Даємо команду намалювати фігуру куб

		// Очищення, для того, щоб інші об'єкти не малювалися цим шейдером
		glBindVertexArray(0);
		glUseProgram(0);
		glfwSwapBuffers(mainWindow); // Міняємо місцями задній буфер (де ми малювали) і передній буфер (який бачить користувач)
	}
	return 0;
}